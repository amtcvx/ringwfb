/*

g++ -std=c++20 -O2 mesh.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o mesh

*/

#include "WiFiDriver.h"
#include "IRadio.h"
#include "TxMode.h"


#include <libusb-1.0/libusb.h>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace std::chrono;

// --- MATÉRIEL : ASUS USB-AC56 (RTL8812AU / Jaguar1) ---
constexpr uint16_t USB_VID = 0x0b05;
constexpr uint16_t USB_PID = 0x17d2;

// --- PARAMÈTRES TDMA ---
constexpr uint8_t MAX_ACTIVE_NODES = 7;        // ID 0 (Coordinateur) + ID 1..6 (Participants)
constexpr uint8_t JOIN_SLOT_INDEX  = 7;        // Slot 7 réservé aux demandes JOIN_REQ (ALOHA)
constexpr uint8_t TOTAL_SLOTS      = 8;        // 8 slots * 2.5 ms = 20 ms par super-trame
constexpr uint64_t SLOT_US         = 2500;
constexpr uint64_t GUARD_US        = 400;
constexpr uint64_t SUPERFRAME_US   = TOTAL_SLOTS * SLOT_US; // 20 ms

// Canaux 5.8 GHz SRD (sans contrainte radar DFS en France/UE)
const std::vector<uint8_t> CANDIDATE_CHANNELS = {149, 153, 157, 161, 165};

enum MsgType : uint8_t {
    MSG_BEACON   = 0, // Slot 0 : Coordinateur -> Tous
    MSG_DATA     = 1, // Slot 1..6 : Nœud attribué -> Tous
    MSG_JOIN_REQ = 2, // Slot 7 : Nouveau nœud -> Coordinateur
    MSG_JOIN_ACK = 3  // Slot 0 (ou piggyback) : Coordinateur -> Nouveau nœud
};

#pragma pack(push, 1)
struct IEEE80211Header {
    uint16_t frameControl = 0x0008;
    uint16_t duration     = 0;
    uint8_t  addr1[6]     = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t  addr2[6]     = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t  addr3[6]     = {0x13, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint16_t seqCtrl      = 0;
};

struct MeshHeader {
    uint8_t  magic;        // 'O', 'M'
    uint8_t  msgType;         // MsgType
    uint8_t  srcNodeId;       // 0..6, ou 0xFF si pas encore attribué
    uint32_t nodeUuid;        // Identifiant aléatoire unique généré au boot
    uint32_t superframeSeq;   // Compteur de super-trame
    uint64_t masterTimeUs;    // Horloge du Coordinateur
    uint8_t  activeChannel;   // Canal en cours
    uint8_t  nextChannel;     // 0 = stable, sinon canal cible pour FastRetune
    uint8_t  countdownSf;     // Compte à rebours de super-trames avant FastRetune
    // Champs spécifiques JOIN_ACK
    uint32_t targetUuid;      // UUID du nœud accepté
    uint8_t  assignedNodeId;  // ID attribué (1..6)
};
#pragma pack(pop)

// --- ÉTAT DU NŒUD ---
struct NodeState {
    uint32_t myUuid = 0;
    std::atomic<uint8_t> myNodeId{0xFF}; // 0xFF = non enregistré
    std::atomic<bool> isCoordinator{false};
    std::atomic<bool> isSynchronized{false};
    std::atomic<uint8_t> currentChannel{149};

    // Synchronisation temporelle
    std::atomic<int64_t> clockOffsetUs{0};
    std::atomic<uint64_t> lastBeaconLocalUs{0};
    std::atomic<uint32_t> lastSuperframeSeq{0};

    // Planification FastRetune
    std::atomic<uint8_t> pendingNextChannel{0};
    std::atomic<uint32_t> targetSwitchSf{0};

    // Métriques de scan & qualité lien
    std::atomic<int> scanRxCount{0};
    std::atomic<int> scanMaxRssi{-120};
    std::atomic<bool> coordinatorDetected{false};
    std::atomic<int> avgRssi{-50};

    // Table des baux (gérée uniquement par le Coordinateur ID 0)
    std::mutex leaseMutex;
    uint32_t slotUuid = {0};
    uint64_t slotLastSeenUs = {0};
} g_state;

uint64_t NowUs() {
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// --- OUVERTURE LIBUSB POUR 0b05:17d2 ---
libusb_device_handle* OpenAsusAC56(libusb_context* ctx) {
    libusb_device_handle* handle = libusb_open_device_with_vid_pid(ctx, USB_VID, USB_PID);
    if (!handle) {
        std::cerr << "[ERR] Adaptateur 0b05:17d2 introuvable (vérifier branchement / permissions udev).\n";
        return nullptr;
    }
    if (libusb_kernel_driver_active(handle, 0) == 1) {
        libusb_detach_kernel_driver(handle, 0);
    }
    if (libusb_claim_interface(handle, 0) < 0) {
        std::cerr << "[ERR] Impossible de réclamer l'interface USB 0.\n";
        libusb_close(handle);
        return nullptr;
    }
    return handle;
}

// --- CONFIGURATION TXPARAMS (RTL8812AU) ---
auto BuildDefaultTxParams() {
    devourer::TxMode mode{};
    mode.mcs = 0;                 // MCS 0 (portée et robustesse maximales)
    mode.bw  = CHANNEL_WIDTH_20;  // 20 MHz (identique à SelectedChannel)
    mode.sgi = false;             // Long Guard Interval
    mode.vht = true;              // Mode 802.11ac VHT (RTL8812AU)
    return tx_mode_to_params(mode);
}

// --- ENVOI DE TRAME ---
void SendMeshFrame(IRadio* radio, uint8_t msgType, uint32_t sfSeq, uint64_t netTimeUs,
                   uint8_t nextCh = 0, uint8_t countdown = 0,
                   uint32_t targetUuid = 0, uint8_t assignedId = 0xFF,
                   const std::vector<uint8_t>& payload = {}) {
    std::vector<uint8_t> buf(sizeof(IEEE80211Header) + sizeof(MeshHeader) + payload.size());

    auto* mac = reinterpret_cast<IEEE80211Header*>(buf.data());
    mac->addr2 = static_cast<uint8_t>(g_state.myUuid & 0xFF);
    mac->addr2 = g_state.myNodeId.load();

    auto* hdr = reinterpret_cast<MeshHeader*>(buf.data() + sizeof(IEEE80211Header));
    hdr->magic         = 'O';
    hdr->magic         = 'M';
    hdr->msgType          = msgType;
    hdr->srcNodeId        = g_state.myNodeId.load();
    hdr->nodeUuid         = g_state.myUuid;
    hdr->superframeSeq    = sfSeq;
    hdr->masterTimeUs     = netTimeUs;
    hdr->activeChannel    = g_state.currentChannel.load();
    hdr->nextChannel      = nextCh;
    hdr->countdownSf      = countdown;
    hdr->targetUuid       = targetUuid;
    hdr->assignedNodeId   = assignedId;

    if (!payload.empty()) {
        std::memcpy(buf.data() + sizeof(IEEE80211Header) + sizeof(MeshHeader), payload.data(), payload.size());
    }

    static TxParams txParams = BuildDefaultTxParams();
    radio->SendPacket(buf.data(), buf.size(), txParams);
}

// --- CALLBACK DE RÉCEPTION (RX) ---
void OnRxPacket(const Packet& pkt) {
    if (pkt.Data.size() < sizeof(IEEE80211Header) + sizeof(MeshHeader)) {
        g_state.scanRxCount++;
        if (pkt.Rssi > g_state.scanMaxRssi) g_state.scanMaxRssi = pkt.Rssi;
        return;
    }

    const auto* hdr = reinterpret_cast<const MeshHeader*>(pkt.Data.data() + sizeof(IEEE80211Header));
    if (hdr->magic != 'O' || hdr->magic != 'M') {
        g_state.scanRxCount++;
        if (pkt.Rssi > g_state.scanMaxRssi) g_state.scanMaxRssi = pkt.Rssi;
        return;
    }

    // Ignorer nos propres paquets (boucle locale USB/RF)
    if (hdr->nodeUuid == g_state.myUuid) return;

    uint64_t rxLocalUs = NowUs();
    g_state.avgRssi = (g_state.avgRssi * 7 + pkt.Rssi) / 8;

    // 1. Réception d'une BALISE (Coordinateur ID 0)
    if (hdr->msgType == MSG_BEACON || hdr->msgType == MSG_JOIN_ACK) {
        g_state.coordinatorDetected = true;
        g_state.currentChannel = hdr->activeChannel;

        int64_t measuredOffset = static_cast<int64_t>(hdr->masterTimeUs) - static_cast<int64_t>(rxLocalUs);
        if (!g_state.isSynchronized) {
            g_state.clockOffsetUs = measuredOffset;
            g_state.isSynchronized = true;
        } else {
            g_state.clockOffsetUs = (g_state.clockOffsetUs * 4 + measuredOffset) / 5;
        }
        g_state.lastBeaconLocalUs = rxLocalUs;
        g_state.lastSuperframeSeq = hdr->superframeSeq;

        // Ordre de saut de fréquence FastRetune programmé par le Coordinateur
        if (hdr->nextChannel != 0 && hdr->countdownSf > 0) {
            g_state.pendingNextChannel = hdr->nextChannel;
            g_state.targetSwitchSf     = hdr->superframeSeq + hdr->countdownSf;
        }

        // Réponse à notre demande d'adhésion JOIN_REQ
        if (hdr->msgType == MSG_JOIN_ACK && hdr->targetUuid == g_state.myUuid) {
            g_state.myNodeId = hdr->assignedNodeId;
            std::cout << "[JOIN] ID attribué par le Coordinateur : Slot " << (int)hdr->assignedNodeId << "\n";
        }
    }

    // 2. Réception d'une demande JOIN_REQ (traitée uniquement par le Coordinateur ID 0)
    if (g_state.isCoordinator && hdr->msgType == MSG_JOIN_REQ) {
        std::lock_guard<std::mutex> lk(g_state.leaseMutex);
        uint8_t assigned = 0xFF;

        // Vérifier si cet UUID a déjà un slot
        for (uint8_t i = 1; i < MAX_ACTIVE_NODES; ++i) {
            if (g_state.slotUuid == hdr->nodeUuid) { assigned = i; break; }
        }
        // Sinon attribuer un slot libre ou expiré (> 2 secondes d'inactivité)
        if (assigned == 0xFF) {
            for (uint8_t i = 1; i < MAX_ACTIVE_NODES; ++i) {
                if (g_state.slotUuid == 0 || (rxLocalUs - g_state.slotLastSeenUs > 2000000)) {
                    g_state.slotUuid = hdr->nodeUuid;
                    assigned = i;
                    break;
                }
            }
        }
        if (assigned != 0xFF) {
            g_state.slotLastSeenUs = rxLocalUs;
            // Stocker pour diffusion immédiate au prochain Slot 0
            g_state.pendingJoinTargetUuid = hdr->nodeUuid;
            g_state.pendingJoinAssignedId = assigned;
            std::cout << "[COORD] Nouveau nœud UUID=" << std::hex << hdr->nodeUuid
                      << std::dec << " -> Slot " << (int)assigned << "\n";
        }
    }

    // 3. Réception de DATA d'un participant
    if (hdr->msgType == MSG_DATA && hdr->srcNodeId >= 1 && hdr->srcNodeId < MAX_ACTIVE_NODES) {
        if (g_state.isCoordinator) {
            std::lock_guard<std::mutex> lk(g_state.leaseMutex);
            g_state.slotLastSeenUs = rxLocalUs;
        }
        // Extraire payload ici...
    }
}

// --- SCAN INITIAL ---
uint8_t DiscoverOrSelectBestChannel(IRadio* radio) {
uint8_t bestChannel = CANDIDATE_CHANNELS;
int bestScore = INT_MAX;
for (uint8_t chNum : CANDIDATE_CHANNELS) {
g_state.scanRxCount = 0;
g_state.scanMaxRssi = -120;
g_state.coordinatorDetected = false;
SelectedChannel ch{ .Channel = chNum, .ChannelOffset = 0, .ChannelWidth = CHANNEL_WIDTH_20 };
radio->SetMonitorChannel(ch); // Calibration RF complète pour scan fiable
std::this_thread::sleep_for(milliseconds(120));
if (g_state.coordinatorDetected) {
g_state.isCoordinator = false;
g_state.myNodeId = 0xFF; // En attente de JOIN_REQ
return chNum;
}
int score = (g_state.scanRxCount * 10) + (g_state.scanMaxRssi + 120);
if (score < bestScore) {
bestScore = score;
bestChannel = chNum;
}
}
// Aucun réseau actif : ce nœud devient Coordinateur (ID 0)
g_state.isCoordinator  = true;
g_state.myNodeId       = 0;
g_state.isSynchronized = true;
std::cout << "[INIT] Promotion en Coordinateur (ID 0) sur canal " << (int)bestChannel << "\n";
return bestChannel;
}
// --- MAIN ---
int main() {
std::random_device rd;
g_state.myUuid = rd();
libusb_context* usbCtx = nullptr;
libusb_init(&usbCtx);
libusb_device_handle* handle = OpenAsusAC56(usbCtx);
if (!handle) return 1;
auto logger = std::make_shared();
WiFiDriver driver(logger);
auto radio = driver.CreateRadio(handle);
SelectedChannel initCh{ .Channel = CANDIDATE_CHANNELS, .ChannelOffset = 0, .ChannelWidth = CHANNEL_WIDTH_20 };
radio->Init(OnRxPacket, initCh);
g_state.currentChannel = DiscoverOrSelectBestChannel(radio.get());
SelectedChannel activeCh{ .Channel = g_state.currentChannel.load(), .ChannelOffset = 0, .ChannelWidth = CHANNEL_WIDTH_20 };
radio->SetMonitorChannel(activeCh);
uint32_t lastTxSf = UINT32_MAX;
uint32_t lastSwitchSf = UINT32_MAX;
std::mt19937 rng(g_state.myUuid);
std::uniform_int_distribution alohaBackoff(1, 4); // Anti-collision JOIN_REQ
int joinSkipCounter = 0;
std::vector<uint8_t> telemetry = {0x01, 0x02, 0x03, 0x04};
while (true) {
uint64_t localUs = NowUs();
// 1. Détection perte Coordinateur (> 300 ms)
if (!g_state.isCoordinator && g_state.isSynchronized && (localUs - g_state.lastBeaconLocalUs > 300000)) {
std::cout << "[WARN] Coordinateur perdu -> Nouveau scan...\n";
g_state.isSynchronized = false;
g_state.myNodeId = 0xFF;
g_state.currentChannel = DiscoverOrSelectBestChannel(radio.get());
activeCh.Channel = g_state.currentChannel.load();
radio->SetMonitorChannel(activeCh);
continue;
}
if (!g_state.isSynchronized) {
std::this_thread::sleep_for(milliseconds(2));
continue;
}
uint64_t netTimeUs = g_state.isCoordinator ? localUs : (localUs + g_state.clockOffsetUs);
uint32_t currentSf = static_cast<uint32_t>(netTimeUs / SUPERFRAME_US);
uint64_t posInSfUs = netTimeUs % SUPERFRAME_US;
// 2. Exécution synchronisée de FastRetune en début de super-trame cible
uint8_t targetCh = g_state.pendingNextChannel.load();
if (targetCh != 0 && currentSf == g_state.targetSwitchSf.load() && lastSwitchSf != currentSf) {
radio->FastRetune(targetCh); // ~1.5 ms sur RTL8812AU (même largeur 20 MHz)
g_state.currentChannel     = targetCh;
g_state.pendingNextChannel = 0;
lastSwitchSf               = currentSf;
std::cout << "[FHSS] FastRetune synchronisé vers canal " << (int)targetCh << "\n";
}
// 3. Déclenchement d'un saut de fréquence par le Coordinateur si SNR/RSSI se dégrade
if (g_state.isCoordinator && g_state.pendingNextChannel == 0 && g_state.avgRssi < -82 && (currentSf % 50 == 0)) {
uint8_t nextCh = CANDIDATE_CHANNELS[(currentSf / 50) % CANDIDATE_CHANNELS.size()];
if (nextCh != g_state.currentChannel) {
g_state.pendingNextChannel = nextCh;
g_state.targetSwitchSf     = currentSf + 5; // Préavis de 5 super-trames (100 ms)
}
}
// 4. Émission TDMA selon le rôle du nœud
if (currentSf != lastTxSf) {
uint8_t myId = g_state.myNodeId.load();
// Cas A : Coordinateur (Slot 0)
if (g_state.isCoordinator && posInSfUs >= GUARD_US && posInSfUs <= (SLOT_US - GUARD_US)) {
uint8_t nextCh   = g_state.pendingNextChannel.load();
uint8_t countDwn = (nextCh != 0 && g_state.targetSwitchSf > currentSf)
? static_cast<uint8_t>(g_state.targetSwitchSf - currentSf) : 0;
// Si un JOIN_REQ est en attente, on envoie un JOIN_ACK (qui sert aussi de balise)
uint32_t joinUuid = 0;
uint8_t  joinId   = 0xFF;
{
std::lock_guardstd::mutex lk(g_state.leaseMutex);
if (g_state.pendingJoinTargetUuid != 0) {
joinUuid = g_state.pendingJoinTargetUuid;
joinId   = g_state.pendingJoinAssignedId;
g_state.pendingJoinTargetUuid = 0;
}
}
uint8_t type = (joinUuid != 0) ? MSG_JOIN_ACK : MSG_BEACON;
SendMeshFrame(radio.get(), type, currentSf, netTimeUs, nextCh, countDwn, joinUuid, joinId, telemetry);
lastTxSf = currentSf;
}
// Cas B : Participant enregistré (Slot 1..6)
else if (!g_state.isCoordinator && myId >= 1 && myId < MAX_ACTIVE_NODES) {
uint64_t start = myId * SLOT_US + GUARD_US;
uint64_t end   = (myId + 1) * SLOT_US - GUARD_US;
if (posInSfUs >= start && posInSfUs <= end) {
SendMeshFrame(radio.get(), MSG_DATA, currentSf, netTimeUs, 0, 0, 0, 0xFF, telemetry);
lastTxSf = currentSf;
}
}
// Cas C : Nouveau nœud sans ID -> Envoi JOIN_REQ dans le Slot 7 (ALOHA avec backoff)
else if (!g_state.isCoordinator && myId == 0xFF) {
uint64_t start = JOIN_SLOT_INDEX * SLOT_US + GUARD_US;
uint64_t end   = (JOIN_SLOT_INDEX + 1) * SLOT_US - GUARD_US;
if (posInSfUs >= start && posInSfUs <= end) {
if (joinSkipCounter <= 0) {
SendMeshFrame(radio.get(), MSG_JOIN_REQ, currentSf, netTimeUs);
joinSkipCounter = alohaBackoff(rng); // Évite les collisions si 2 nœuds bootent ensemble
} else {
joinSkipCounter--;
}
lastTxSf = currentSf;
}
}
}
std::this_thread::sleep_for(microseconds(150));
}
radio->Stop();
libusb_release_interface(handle, 0);
libusb_close(handle);
libusb_exit(usbCtx);
return 0;
}
