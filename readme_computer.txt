ubuntu

git clone https://github.com/amtcvx/ringwfb.git
cd ringwfb
./install.sh

sudo nmcli connection add type ethernet con-name myusbconnection match.interface-name "enx*" ipv4.addresses "192.168.3.1/24" ipv4.method manual connection.autoconnect yes

sudo nmcli connection up myusbconnection
