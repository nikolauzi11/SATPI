/* InterfaceAttr.h

   Copyright (C) 2014 - 2020 Marc Postema (mpostema09 -at- gmail.com)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html
*/
#ifndef INTERFACEATTR_H_INCLUDE
#define INTERFACEATTR_H_INCLUDE INTERFACEATTR_H_INCLUDE

#include <string>

/// Interface attributes
class InterfaceAttr {
	public:
		// =====================================================================
		//  -- Constructors and destructor -------------------------------------
		// =====================================================================

		///
		/// @param bindInterfaceName specifies the network interface name to bind to
		explicit InterfaceAttr(const std::string &bindInterfaceName);

		virtual ~InterfaceAttr();

		// =====================================================================
		//  -- Static member functions -----------------------------------------
		// =====================================================================

	public:

		/// Get the default netowrk buffer size for UDP packets
		static int getNetworkUDPBufferSize();

		// =====================================================================
		//  -- Other member functions ------------------------------------------
		// =====================================================================

	public:

		/// Get the default netowrk buffer size for UDP packets
		void bindToInterfaceName(const std::string &ifaceName);

		/// Get the IP address of the used interface
		const std::string &getIPAddress() const {
			return _ipAddr;
		}

		/// Get the UUID of this device
		std::string getUUID() const;

		// =====================================================================
		//  -- Data members ----------------------------------------------------
		// =====================================================================

	protected:

		std::string _ipAddr;           /// ip address of the used interface
		std::string _macAddrDecorated; /// mac address of the used interface
		std::string _macAddr;          /// mac address of the used interface
		std::string _ifaceName;        /// used interface name i.e. eth0
};

#endif // INTERFACEATTR_H_INCLUDE
