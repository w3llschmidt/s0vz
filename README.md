S0/Impulse to Volkszaehler daemon.
==================================

This is a Linux SYSFS daemon. Written in ANSI C, provides low memory signature and minimal CPU load.  
Its fully interrupt driven with no cpu eating idle loops!

Hardware by Udo S.  
http://wiki.volkszaehler.org/hardware/controllers/raspberry_pi_erweiterung

![My image](http://wiki.volkszaehler.org/_media/hardware/controllers/raspi_6xs0_3x1-wire_1xir_bestueckt.png?w=200)  

Backend-Software  
http://volkszaehler.org/

![My image](http://wiki.volkszaehler.org/_media/software/releases/demo-screenshot.jpg?w=300)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Installation
============

Precondition: Raspian Linux (http://www.raspberrypi.org/downloads) 

---

curl & libconfig	-> 'sudo apt-get install libcurl4-gnutls-dev libconfig-dev'

The curl & libconfig is only needed if you compile the source! If you use the binary you dont need them!

---

firmware update!	-> https://github.com/Hexxeh/rpi-update

---

s0vz.c 	 	-> /tmp ( sudo gcc -o /usr/sbin/s0vz s0vz.c -lconfig -lcurl )

s0vz.cfg	 	-> /etc/  

rc.local  	-> /etc/  ( ! add this settings, dont overwrite your exisiting rc.local ! )

s0vz 	 	-> /etc/init.d/ ( start/stop/restart )

Configuration
=============

$ sudo insserv s0vz ( make autostart )

$ sudo vim /etc/s0vz.cfg ( edit your config )

Reboot and check '/var/log/syslog' for outputs! 




