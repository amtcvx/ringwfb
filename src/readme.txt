#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>

static unsigned int my_détournement_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
    struct net_device *wifi_dev;
    struct radiotap_hdr_minimal rt_hdr;
    
    // Taille des nouveaux en-têtes à insérer : 
    // 8 octets (Radiotap) + 24 octets (Header IEEE 802.11 Data minimum) = 32 octets
    unsigned int needed_headroom = sizeof(struct radiotap_hdr_minimal) + 24; 
    int err;

    if (!skb) return NF_ACCEPT;

    // 1. Récupérer l'interface Wi-Fi cible
    wifi_dev = dev_get_by_name(&init_net, "wlan0mon");
    if (!wifi_dev) {
        // Si l'interface Wi-Fi n'est pas disponible, on drop le paquet 
        // pour éviter qu'il ne continue sa route (conformément au but du détournement)
        return NF_DROP; 
    }

    // 2. Agrandir le headroom du skb actuel sur place
    // pskb_expand_head gère le Copy-On-Write si le buffer est partagé (cloné)
    err = pskb_expand_head(skb, needed_headroom, 0, GFP_ATOMIC);
    if (err) {
        dev_put(wifi_dev);
        return NF_DROP; // Échec critique d'allocation mémoire
    }

    // 3. Déplacer le pointeur data vers la gauche pour réserver l'espace des nouveaux en-têtes
    skb_push(skb, needed_headroom);

    // 4. Écrire l'en-tête Radiotap au tout début (offset 0 du nouveau skb->data)
    rt_hdr.version = 0;
    rt_hdr.pad     = 0;
    rt_hdr.length  = cpu_to_le16(sizeof(struct radiotap_hdr_minimal));
    rt_hdr.present = cpu_to_le32(0);
    memcpy(skb->data, &rt_hdr, sizeof(struct radiotap_hdr_minimal));

    // 5. Écrire l'en-tête MAC 802.11 (offset 8 du nouveau skb->data)
    unsigned char *wlan_hdr = skb->data + sizeof(struct radiotap_hdr_minimal);
    
    // Structure d'un en-tête 802.11 Data standard (Exemple conceptuel)
    wlan_hdr[0] = 0x08; // Frame Control: Type = Data, Subtype = 0
    wlan_hdr[1] = 0x01; // Flags: To-DS = 1, From-DS = 0 (Envoi vers un Point d'Accès)
    wlan_hdr[2] = 0x00; wlan_hdr[3] = 0x00; // Duration
    
    // Adresse 1 (RA/BSSID) - Ex: MAC de votre Point d'Accès
    memcpy(&wlan_hdr[4], "\x00\x11\x22\x33\x44\x55", 6); 
    // Adresse 2 (TA/SA) - Ex: MAC de la station émettrice simulée
    memcpy(&wlan_hdr[10], "\x66\x77\x88\x99\xaa\xbb", 6);
    // Adresse 3 (DA) - Ex: MAC de la destination finale (souvent la même que le BSSID ou la passerelle)
    memcpy(&wlan_hdr[16], "\x00\x11\x22\x33\x44\x55", 6);
    
    wlan_hdr[22] = 0x00; wlan_hdr[23] = 0x00; // Sequence Control

    // À ce stade, juste après wlan_hdr[23], se trouve immédiatement l'ancien en-tête IP 
    // (le payload d'origine du skb), ce qui est parfaitement aligné pour une trame Wi-Fi Data.

    // 6. Recalculer la longueur totale du paquet (skb->len a été augmenté de needed_headroom)
    // Le pilote Wi-Fi se basera sur skb->len pour savoir combien d'octets envoyer sur l'antenne.

    // 7. Configurer les paramètres réseau pour le pilote
    skb->dev = wifi_dev;
    skb->protocol = htons(ETH_P_80211_RAW); // Mode moniteur brut
    skb->pkt_type = PACKET_OUTGOING;

    // Réinitialiser les offsets internes du skb
    skb_reset_mac_header(skb);
    skb_set_network_header(skb, sizeof(struct radiotap_hdr_minimal) + 24);

    // 8. Envoyer directement au pilote (Bypasse le protocole mac80211 standard et qdisc)
    // dev_direct_xmit prend la responsabilité exclusive de libérer ce skb.
    dev_direct_xmit(skb, 0);

    dev_put(wifi_dev);

    // 9. LE VERDICT CRUCIAL : NF_STOLEN
    // On indique à Netfilter que le skb a été "volé" par notre fonction. 
    // La pile IP ne tentera plus de le lire, de le transférer ou de le libérer.
    return NF_STOLEN;
}




clear;make clean;make
sudo cp wfb_nfkernel.ko /lib/modules/7.0.0-38-generic/
sudo depmod -A

sudo modprobe wfb_nfkernel 
modprobe: ERROR: could not insert 'wfb_nfkernel': Operation not permitted

sudo modprobe -r wfb_nfkernel 

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1


0	1	2412
1	2	2417
2	3	2422
3	4	2427
4	5	2432
5	6	2437
6	7	2442
7	8	2447
8	9	2452
9	10	2457
10	11	2462
11	36	5180
12	40	5200
13	44	5220
14	48	5240
15	52	5260
16	56	5280
17	60	5300
18	64	5320
19	100	5500
20	104	5520
21	108	5540
22	112	5560
23	116	5580
24	120	5600
25	124	5620
26	128	5640
27	132	5660
28	136	5680
29	140	5700
30	144	5720
31	149	5745
32	153	5765
33	157	5785
34	161	5805
35	165	5825

