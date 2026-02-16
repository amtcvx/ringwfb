This project provides wireless communication between ground / board and board / board systems.

-------------------------------------------------------------------------------
git clone --recurse-submodules  http://github.com/amtcvx/ringwfb.git
(git clone --recurse-submodules  git@github.com:amtcvx/ringwfb.git)

Installation need internet connection and sudo 
./install.sh
(./uninstall.sh)

-------------------------------------------------------------------------------
Before first run, set Makefile according to the target role

For unique ground control station set
  ROLEFLAG += -DDRONEID=0
For each boards set
  ROLEFLAG += -DDRONEID=1
  ...

make clean;make all

-------------------------------------------------------------------------------

