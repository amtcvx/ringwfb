#!/usr/bin/env python3

#sudo apt install -y python3-scapy

from scapy.all import sendp
from scapy.utils import hexdump

IEEE80211_RADIOTAP_MCS_HAVE_BW   = 0x01  # Bande passante renseignée
IEEE80211_RADIOTAP_MCS_HAVE_MCS  = 0x02  # Index MCS renseigné
IEEE80211_RADIOTAP_MCS_HAVE_GI   = 0x04  # Intervalle de garde (GI) renseigné
IEEE80211_RADIOTAP_MCS_HAVE_STBC = 0x20

MCS_KNOWN = ( IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )

IEEE80211_RADIOTAP_MCS_BW_20 = 0
IEEE80211_RADIOTAP_MCS_SGI = 0x04
IEEE80211_RADIOTAP_MCS_STBC_1 = 1
IEEE80211_RADIOTAP_MCS_STBC_SHIFT = 5

MCS_FLAGS = (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))

MCS_INDEX = 2


radiotaphd = b'\x00\x00\x0d\x00\x00\x80\x08\x00\x08\x00'
radiotaphd += bytes([MCS_KNOWN])
radiotaphd += bytes([MCS_FLAGS])
radiotaphd += bytes([MCS_INDEX])

#Data (type=2) with QOS (subtype=8)
ieeeiqoshd = b'\x88\x00\x00\x00\x36\x35\x34\x33\x32\x31\x26\x25\x24\x23\x22\x21\x16\x15\x14\x13\x12\x11\x00\x00\x20\x00'

payload = "HELLO".encode("utf-8")

pkt = radiotaphd + ieeeiqoshd + payload

hexdump(pkt)

sendp(pkt, iface='wlxfc349725a317')
