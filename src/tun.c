#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Module TUN bidirectionnel en espace noyau");

static struct net_device *my_tun_dev;

/* =========================================================================
 * 1. INTERCEPTION (SORTIE - TX)
 * ========================================================================= */
static netdev_tx_t my_tun_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct iphdr *iph;

    if (!skb)
        return NETDEV_TX_OK;

    // Récupération de l'en-tête IP (Interface TUN = Pas d'en-tête Ethernet)
    iph = ip_hdr(skb);
    if (iph) {
        pr_info("[TUN TX] Sortie détectée: %pI4 -> %pI4 (Taille: %d, Proto: %d)\n",
                &iph->saddr, &iph->daddr, ntohs(iph->tot_len), iph->protocol);
    }

    // Mise à jour des statistiques de l'interface
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;

    // Libération obligatoire du skb si le module consomme ou redirige le paquet
    dev_kfree_skb(skb);

    return NETDEV_TX_OK; 
}

/* =========================================================================
 * 2. INJECTION (ENTRÉE - RX)
 * ========================================================================= */
/**
 * @brief Injecte un paquet IP brut dans la pile réseau Linux (Simule une réception)
 * @param ip_data Pointeur vers le tableau d'octets contenant le paquet IP brut
 * @param len Taille du paquet IP en octets
 */
int injecter_paquet_entrant(const unsigned char *ip_data, unsigned int len)
{
    struct sk_buff *skb;

    if (!my_tun_dev)
        return -ENODEV;

    // 1. Allouer un sk_buff adapté au noyau
    skb = dev_alloc_skb(len);
    if (!skb) {
        pr_err("[TUN RX] Échec d'allocation du sk_buff\n");
        return -ENOMEM;
    }

    // 2. Associer le sk_buff à notre interface virtuelle
    skb->dev = my_tun_dev;

    // 3. Copier les données IP brutes dans la zone mémoire du skb
    skb_put_data(skb, ip_data, len);

    // 4. Configurer les métadonnées indispensables pour le mode TUN (Couche 3)
    skb_reset_network_header(skb);                  // Le paquet commence à l'en-tête IP
    skb->protocol = htons(ETH_P_IP);                // Indique explicitement que c'est de l'IPv4
    skb->pkt_type = PACKET_HOST;                    // Destiné à notre propre machine

    // 5. Mettre à jour les statistiques de réception
    my_tun_dev->stats.rx_packets++;
    my_tun_dev->stats.rx_bytes += len;

    // 6. Injecter le paquet dans la pile réseau Linux
    // netif_rx prend le relais et libère automatiquement le skb après traitement
    return netif_rx(skb);
}
EXPORT_SYMBOL(injecter_paquet_entrant); // Permet à un autre module de l'appeler si besoin

/* =========================================================================
 * CONFIGURATION ET INITIALISATION DE L'INTERFACE
 * ========================================================================= */
static const struct net_device_ops my_netdev_ops = {
    .ndo_start_xmit = my_tun_start_xmit,
};

static void my_tun_setup(struct net_device *dev)
{
    dev->netdev_ops       = &my_netdev_ops;
    dev->type             = ARPHRD_NONE;     // Mode TUN (Pas d'adresse MAC / Pas d'ARP)
    dev->mtu              = 1500;
    dev->addr_len         = 0;
    dev->flags            = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
    dev->tx_queue_len     = 500;
}

static int __init my_tun_init(void)
{
    int err;

    my_tun_dev = alloc_netdev(0, "tun%d", NET_NAME_UNKNOWN, my_tun_setup);
    if (!my_tun_dev)
        return -ENOMEM;

    err = register_netdev(my_tun_dev);
    if (err) {
        free_netdev(my_tun_dev);
        return err;
    }

    pr_info("Interface bidirectionnelle %s enregistrée.\n", my_tun_dev->name);
    return 0;
}

static void __exit my_tun_exit(void)
{
    if (my_tun_dev) {
        unregister_netdev(my_tun_dev);
        free_netdev(my_tun_dev);
        pr_info("Interface TUN bidirectionnelle désenregistrée.\n");
    }
}

module_init(my_tun_init);
module_exit(my_tun_exit);

