#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AI Assistant");
MODULE_DESCRIPTION("Injection Wi-Fi QoS directe avec Radiotap VHT et dev_direct_xmit");
MODULE_VERSION("2.0");

#define RADIOTAP_VHT_LEN 24
#define IEEE80211_QOS_MAC_LEN 26

// ---- PARAMÈTRES DYNAMIQUES DU MODULE (Modifiables via /sys) ----
static int target_mcs = 7;
module_param(target_mcs, int, 0644);
MODULE_PARM_DESC(target_mcs, "Index VHT-MCS dynamique (0-8 pour le mode 20MHz)");

static int qos_tid = 7; // TID 7 = Priorité maximale (Voice)
module_param(qos_tid, int, 0644);
MODULE_PARM_DESC(qos_tid, "Priorité QoS TID (0-7, 7 = Priorité Voix maximale)");

static char *target_iface = "wlan0";
module_param(target_iface, charp, 0644);
MODULE_PARM_DESC(target_iface, "Nom de l'interface Wi-Fi en mode Monitor (ex: wlan0)");

// Structure globale pour le hook Netfilter
static struct nf_hook_ops *nf_ops = NULL;

// Structure de l'en-tête MAC 802.11 QoS Data 
struct ieee80211_qos_header {
    u8 frame_control[2]; // 0x88, 0x00 pour du QoS Data
    u8 duration_id[2];   // 0x00, 0x00
    u8 addr1[6];         // Adresse MAC Destination (ex: Broadcast FF:FF:FF:FF:FF:FF)
    u8 addr2[6];         // Adresse MAC Source (Émetteur)
    u8 addr3[6];         // Adresse MAC BSSID (Point d'accès)
    u8 seq_ctrl[2];      // Numéro de séquence (0x00, 0x00)
    u8 qos_ctrl[2];      // QoS Control : Contient le TID de priorité
} __attribute__((packed));

// Fonction de construction dynamique de l'en-tête Radiotap VHT
static int inject_radiotap_vht(struct sk_buff *skb, int mcs_index)
{
    u8 rtap[RADIOTAP_VHT_LEN];
    u8 mcs_nss_octet;

    if (mcs_index < 0 || mcs_index > 8)
        mcs_index = 7; // Repli de sécurité

    // Calcul de mcs_nss : (MCS << 4) | NSS (NSS=1 pour 1 antenne active)
    mcs_nss_octet = (u8)((mcs_index << 4) | 1);

    // 1. En-tête fixe Radiotap + Flags d'injection (12 octets)
    rtap[0] = 0x00; rtap[1] = 0x00;                         // it_version, it_pad
    rtap[2] = RADIOTAP_VHT_LEN; rtap[3] = 0x00;             // it_len (24 octets)
    rtap[4] = 0x00; rtap[5] = 0x40; rtap[6] = 0x20; rtap[7] = 0x00; // it_present: TX_FLAGS + VHT
    rtap[8] = 0x08; rtap[9] = 0x00;                         // TX_FLAGS: RADIOTAP_F_TX_NOACK (Pas d'ACK)
    rtap[10] = 0x00; rtap[11] = 0x00;                       // Bourrage interne d'alignement

    // 2. Bloc VHT (12 octets) basé sur vos spécifications (0x27, 0x24, 0x02, 0x00)
    rtap[12] = 0x27; rtap[13] = 0x24;                       // known: Masque VHT (0x2427)
    rtap[14] = 0x02;                                        // flags: Short GI activé (0x02)
    rtap[15] = 0x00;                                        // bandwidth: Forcer le mode 20 MHz (0x00)
    rtap[16] = mcs_nss_octet;                               // User 0: Configuration MCS dynamique calculée
    rtap[17] = 0x00; rtap[18] = 0x00; rtap[19] = 0x00;       // Users 1, 2, 3 inactifs
    rtap[20] = 0x00;                                        // coding: BCC
    rtap[21] = 0x00;                                        // group_id: Single User
    rtap[22] = 0x00; rtap[23] = 0x00;                       // partial_aid

    if (skb_headroom(skb) < RADIOTAP_VHT_LEN)
        return -ENOMEM;

    u8 *header_space = skb_push(skb, RADIOTAP_VHT_LEN);
    memcpy(header_space, rtap, RADIOTAP_VHT_LEN);
    return 0;
}

// Fonction de callback Netfilter
static unsigned int qos_injector_hook(void *priv,
                                      struct sk_buff *skb,
                                      const struct nf_hook_state *state)
{
    struct iphdr *iph;
    struct net_device *target_dev;

    if (!skb)
        return NF_ACCEPT;

    iph = ip_hdr(skb);
    if (!iph)
        return NF_ACCEPT;

    // Exemple de filtrage : interception si l'IP de destination est 1.2.3.4
    if (iph->daddr == htonl(0x01020304)) {
        pr_info("[QoS_Injector] Paquet vers 1.2.3.4 intercepté.\n");

        // Récupérer l'interface réseau cible configurée (ex: wlan0)
        target_dev = dev_get_by_name(&init_net, target_iface);
        if (target_dev) {
            
            // Cloner le paquet pour travailler sur une copie isolée
            struct sk_buff *nskb = skb_clone(skb, GFP_ATOMIC);
            if (nskb) {
                nskb->dev = target_dev;
                nskb->protocol = htons(ETH_P_802_2); // Assigner le mode LLC/Raw 802.11

                // Vérifier s'il y a assez d'espace cumulé (Radiotap + MAC Wi-Fi) en headroom
                size_t total_needed = RADIOTAP_VHT_LEN + IEEE80211_QOS_MAC_LEN;
                if (skb_headroom(nskb) < total_needed) {
                    struct sk_buff *expanded = skb_copy_expand(nskb, total_needed, 0, GFP_ATOMIC);
                    kfree_skb(nskb);
                    if (!expanded) {
                        dev_put(target_dev);
                        return NF_DROP;
                    }
                    nskb = expanded;
                }

                // ---- ÉTAPE 1 : Forger l'en-tête MAC Wi-Fi QoS ----
                struct ieee80211_qos_header *qos_mac = (struct ieee80211_qos_header *)skb_push(nskb, IEEE80211_QOS_MAC_LEN);
                
                qos_mac->frame_control[0] = 0x88; // Subtype: QoS Data (0x08), Type: Data (0x02) -> 0x88
                qos_mac->frame_control[1] = 0x00;
                qos_mac->duration_id[0]   = 0x00; qos_mac->duration_id[1]   = 0x00;
                
                // Remplissage d'adresses MAC factices (à adapter selon vos scans/BSSID)
                memset(qos_mac->addr1, 0xFF, 6); // Destination : Broadcast
                memset(qos_mac->addr2, 0x11, 6); // Source
                memset(qos_mac->addr3, 0x22, 6); // BSSID
                
                qos_mac->seq_ctrl[0]      = 0x00; qos_mac->seq_ctrl[1]      = 0x00;
                qos_mac->qos_ctrl[0]      = (u8)(qos_tid & 0x07); // Injection de la priorité TID (0-7)
                qos_mac->qos_ctrl[1]      = 0x00;

                // ---- ÉTAPE 2 : Forger et pousser l'en-tête Radiotap VHT ----
                if (inject_radiotap_vht(nskb, target_mcs) == 0) {
                    
                    // Réaligner les structures MAC du noyau avant transmission
                    skb_reset_mac_header(nskb);

                    // ---- ÉTAPE 3 : Transmission directe vers le pilote Realtek ----
                    int rc = dev_direct_xmit(nskb, 0); // 0 = Première file d'attente matérielle
                    
                    if (rc == NET_XMIT_SUCCESS) {
                        pr_info("[QoS_Injector] dev_direct_xmit : Succès (MCS %d, TID %d).\n", target_mcs, qos_tid);
                    } else {
                        pr_warn("[QoS_Injector] Échec de dev_direct_xmit, code: %d\n", rc);
                    }
                } else {
                    kfree_skb(nskb);
                }
            }
            dev_put(target_dev);
        }
        
        // Supprimer le paquet réseau standard d'origine
        return NF_DROP; 
    }

    return NF_ACCEPT;
}

// Initialisation du module
static int __init qos_injector_init(void)
{
    nf_ops = kcalloc(1, sizeof(struct nf_hook_ops), GFP_KERNEL);
    if (!nf_ops)
        return -ENOMEM;

    nf_ops->hook     = qos_injector_hook;
    nf_ops->hooknum  = NF_INET_LOCAL_OUT;
    nf_ops->pf       = NFPROTO_IPV4;
    nf_ops->priority = NF_IP_PRI_FIRST; // Priorité absolue sur Netfilter

    int ret = nf_register_net_hook(&init_net, nf_ops);
    if (ret < 0) {
        kfree(nf_ops);
        pr_err("[QoS_Injector] Erreur d'enregistrement Netfilter.\n");
        return ret;
    }

    pr_info("[QoS_Injector] Module chargé. Interface cible: %s. Prêt pour interception.\n", target_iface);
    return 0;
}

// Déchargement du module
static void __exit qos_injector_exit(void)
{
    if (nf_ops) {
        nf_unregister_net_hook(&init_net, nf_ops);
        kfree(nf_ops);
        pr_info("[QoS_Injector] Module déchargé proprement.\n");
    }
}

module_init(qos_injector_init);
module_exit(qos_injector_exit);
