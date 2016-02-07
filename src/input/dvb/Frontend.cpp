/* Frontend.cpp

   Copyright (C) 2015, 2016 Marc Postema (mpostema09 -at- gmail.com)

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
#include <input/dvb/Frontend.h>

#include <Log.h>
#include <Utils.h>
#include <Stream.h>
#include <StringConverter.h>
#include <StreamProperties.h>
#include <mpegts/PacketBuffer.h>
#include <input/dvb/delivery/DVB_S.h>

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/dvb/dmx.h>

namespace input {
namespace dvb {

	Frontend::Frontend() :
		_tuned(false),
		_fd_fe(-1),
		_fd_dvr(-1) {
		snprintf(_fe_info.name, sizeof(_fe_info.name), "Not Set");
		_path_to_fe  = "Not Set";
		_path_to_dvr = "Not Set";
		_path_to_dmx = "Not Set";
		_deliverySystem = new input::dvb::delivery::DVB_S;
	}

	Frontend::~Frontend() {
		DELETE(_deliverySystem);
	}

	// =======================================================================
	//  -- Static member functions -------------------------------------------
	// =======================================================================

	void Frontend::enumerate(StreamVector &stream, decrypt::dvbapi::Client *decrypt,
		const std::string &path, int &nr_dvb_s2, int &nr_dvb_t, int &nr_dvb_t2,
		int &nr_dvb_c, int &nr_dvb_c2) {
		SI_LOG_INFO("Detecting frontends in: %s", path.c_str());
		const int count = getAttachedFrontends(stream, decrypt, path, 0);
		SI_LOG_INFO("Frontends found: %zu", count);
		countNumberOfDeliverySystems(stream, nr_dvb_s2, nr_dvb_t,
			nr_dvb_t2, nr_dvb_c, nr_dvb_c2);
	}

	void Frontend::countNumberOfDeliverySystems(StreamVector &stream, int &nr_dvb_s2,
			int &nr_dvb_t, int &nr_dvb_t2, int &nr_dvb_c, int &nr_dvb_c2) {
		for (StreamVector::iterator it = stream.begin(); it != stream.end(); ++it) {
			const Stream *stream = *it;
			int dvb_s2 = 0;
			int dvb_t  = 0;
			int dvb_t2 = 0;
			int dvb_c  = 0;
			int dvb_c2 = 0;
			//
			for (size_t j = 0; j < stream->getDeliverySystemSize(); j++) {
				const fe_delivery_system_t *del_sys = stream->getDeliverySystem();
				switch (del_sys[j]) {
					// only count DVBS2
					case SYS_DVBS2:
						++dvb_s2;
						break;
					case SYS_DVBT:
						++dvb_t;
						break;
					case SYS_DVBT2:
						++dvb_t2;
						break;
#if FULL_DVB_API_VERSION >= 0x0505
					case SYS_DVBC_ANNEX_A:
					case SYS_DVBC_ANNEX_B:
					case SYS_DVBC_ANNEX_C:
						if (dvb_c == 0) {
							++dvb_c;
						}
						break;
#else
					case SYS_DVBC_ANNEX_AC:
					case SYS_DVBC_ANNEX_B:
						if (dvb_c == 0) {
							++dvb_c;
						}
						break;
#endif
					default:
						// Not supported
						break;
				}
			}
			nr_dvb_s2 += dvb_s2;
			nr_dvb_t  += dvb_t;
			nr_dvb_t2 += dvb_t2;
			nr_dvb_c  += dvb_c;
			nr_dvb_c2 += dvb_c2;
		}
	}

	int Frontend::getAttachedFrontends(StreamVector &stream, decrypt::dvbapi::Client *decrypt,
			const std::string &path, int count) {
#define DMX      "/dev/dvb/adapter%d/demux%d"
#define DVR      "/dev/dvb/adapter%d/dvr%d"
#define FRONTEND "/dev/dvb/adapter%d/frontend%d"

#define FE_PATH_LEN 255
#if SIMU
		UNUSED(path)
		count = 2;
		char fe_path[FE_PATH_LEN];
		char dvr_path[FE_PATH_LEN];
		char dmx_path[FE_PATH_LEN];
		snprintf(fe_path,  FE_PATH_LEN, FRONTEND, 0, 0);
		snprintf(dvr_path, FE_PATH_LEN, DVR, 0, 0);
		snprintf(dmx_path, FE_PATH_LEN, DMX, 0, 0);
		stream.push_back(new Stream(0, decrypt));
		stream[0]->addFrontendPaths(fe_path, dvr_path, dmx_path);
		stream[0]->setFrontendInfo();
		snprintf(fe_path,  FE_PATH_LEN, FRONTEND, 1, 0);
		snprintf(dvr_path, FE_PATH_LEN, DVR, 1, 0);
		snprintf(dmx_path, FE_PATH_LEN, DMX, 1, 0);
		stream.push_back(new Stream(1, decrypt));
		stream[1]->addFrontendPaths(fe_path, dvr_path, dmx_path);
		stream[1]->setFrontendInfo();
#else
		struct dirent **file_list;
		const int n = scandir(path.c_str(), &file_list, nullptr, alphasort);
		if (n > 0) {
			int i;
			for (i = 0; i < n; ++i) {
				char full_path[FE_PATH_LEN];
				snprintf(full_path, FE_PATH_LEN, "%s/%s", path.c_str(), file_list[i]->d_name);
				struct stat stat_buf;
				if (stat(full_path, &stat_buf) == 0) {
					switch (stat_buf.st_mode & S_IFMT) {
						case S_IFCHR: // character device
							if (strstr(file_list[i]->d_name, "frontend") != nullptr) {
								int fe_nr;
								sscanf(file_list[i]->d_name, "frontend%d", &fe_nr);
								int adapt_nr;
								sscanf(path.c_str(), "/dev/dvb/adapter%d", &adapt_nr);

								// make new paths
								char fe_path[FE_PATH_LEN];
								char dvr_path[FE_PATH_LEN];
								char dmx_path[FE_PATH_LEN];
								snprintf(fe_path,  FE_PATH_LEN, FRONTEND, adapt_nr, fe_nr);
								snprintf(dvr_path, FE_PATH_LEN, DVR, adapt_nr, fe_nr);
								snprintf(dmx_path, FE_PATH_LEN, DMX, adapt_nr, fe_nr);

								stream.push_back(new Stream(count, decrypt));
								stream[count]->addFrontendPaths(fe_path, dvr_path, dmx_path);
								stream[count]->setFrontendInfo();

								++count;
							}
							break;
						case S_IFDIR:
							// do not use dir '.' an '..'
							if (strcmp(file_list[i]->d_name, ".") != 0 && strcmp(file_list[i]->d_name, "..") != 0) {
								count = getAttachedFrontends(stream, decrypt, full_path, count);
							}
							break;
					}
				}
				free(file_list[i]);
			}
		}
#endif
#undef DMX
#undef DVR
#undef FRONTEND
#undef FE_PATH_LEN
		return count;
	}

	// =======================================================================
	//  -- base::XMLSupport --------------------------------------------------
	// =======================================================================

	void Frontend::addToXML(std::string &xml) const {
		StringConverter::addFormattedString(xml, "<frontendname>%s</frontendname>", _fe_info.name);
		StringConverter::addFormattedString(xml, "<pathname>%s</pathname>", _path_to_fe.c_str());
		StringConverter::addFormattedString(xml, "<freq>%d Hz to %d Hz</freq>", _fe_info.frequency_min, _fe_info.frequency_max);
		StringConverter::addFormattedString(xml, "<symbol>%d symbols/s to %d symbols/s</symbol>", _fe_info.symbol_rate_min, _fe_info.symbol_rate_max);

		ADD_XML_BEGIN_ELEMENT(xml, "deliverySystem");
		_deliverySystem->addToXML(xml);
		ADD_XML_END_ELEMENT(xml, "deliverySystem");
	}

	void Frontend::fromXML(const std::string &UNUSED(xml)) {}

	// =======================================================================
	//  -- input::Device------------------------------------------------------
	// =======================================================================
	bool Frontend::isDataAvailable() {
		struct pollfd pfd[1];
		pfd[0].fd = _fd_dvr;
		pfd[0].events = POLLIN | POLLPRI;
		pfd[0].revents = 0;
		return poll(pfd, 1, 100) > 0;
	}

	bool Frontend::readFullTSPacket(mpegts::PacketBuffer &buffer) {
		// try read maximum amount of bytes from DVR
		const int bytes = read(_fd_dvr, buffer.getWriteBufferPtr(), buffer.getAmountOfBytesToWrite());
		if (bytes > 0) {
			buffer.addAmountOfBytesWritten(bytes);
			return buffer.full();
		}
		return false;
	}

	bool Frontend::capableOf(fe_delivery_system_t msys) {
		for (std::size_t i = 0; i < MAX_DELSYS; ++i) {
			// we no not like SYS_UNDEFINED
			if (_info_del_sys[i] != SYS_UNDEFINED && msys == _info_del_sys[i]) {
				return true;
			}
		}
		return false;
	}

	void Frontend::monitorFrontend(fe_status_t &status, uint16_t &strength, uint16_t &snr, uint32_t &ber, uint32_t &ublocks, bool showStatus) {
		// first read status
		if (ioctl(_fd_fe, FE_READ_STATUS, &status) == 0) {
			// some frontends might not support all these ioctls
			if (ioctl(_fd_fe, FE_READ_SIGNAL_STRENGTH, &strength) != 0) {
				strength = 0;
			}
			if (ioctl(_fd_fe, FE_READ_SNR, &snr) != 0) {
				snr = 0;
			}
			if (ioctl(_fd_fe, FE_READ_BER, &ber) != 0) {
				ber = 0;
			}
			if (ioctl(_fd_fe, FE_READ_UNCORRECTED_BLOCKS, &ublocks) != 0) {
				ublocks = 0;
			}
			strength = (strength * 240) / 0xffff;
			snr = (snr * 15) / 0xffff;

			// Print Status
			if (showStatus) {
				SI_LOG_INFO("status %02x | signal %3u%% | snr %3u%% | ber %d | unc %d | Locked %d",
					status, strength, snr, ber, ublocks,
					(status & FE_HAS_LOCK) ? 1 : 0);
			}
		} else {
			PERROR("FE_READ_STATUS failed");
		}
	}

	bool Frontend::update(StreamProperties &properties) {
		SI_LOG_DEBUG("Stream: %d, Updating frontend...", properties.getStreamID());
#ifndef SIMU
		// Setup, tune and set PID Filters
		if (properties.hasChannelDataChanged()) {
			properties.resetChannelDataChanged();
			_tuned = false;
			CLOSE_FD(_fd_dvr);
		}

		std::size_t timeout = 0;
		while (!setupAndTune(properties)) {
			usleep(150000);
			++timeout;
			if (timeout > 3) {
				return false;
			}
		}

		updatePIDFilters(properties);
#endif

		SI_LOG_DEBUG("Stream: %d, Updating frontend (Finished)", properties.getStreamID());
		return true;
	}

	bool Frontend::teardown(StreamProperties &properties) {
		const int streamID = properties.getStreamID();
		for (std::size_t i = 0; i < MAX_PIDS; ++i) {
			if (properties.isPIDUsed(i)) {
				SI_LOG_DEBUG("Stream: %d, Remove filter PID: %04d - fd: %03d - Packet Count: %d",
						streamID, i, properties.getDMXFileDescriptor(i), properties.getPacketCounter(i));
				resetPid(properties, i);
			} else if (properties.getDMXFileDescriptor(i) != -1) {
				SI_LOG_ERROR("Stream: %d, !! No PID %d but still open DMX !!", streamID, i);
				resetPid(properties, i);
			}
		}
		_tuned = false;
		CLOSE_FD(_fd_fe);
		CLOSE_FD(_fd_dvr);
		return true;
	}

	// =======================================================================
	//  -- Other member functions --------------------------------------------
	// =======================================================================
	int Frontend::open_fe(const std::string &path, bool readonly) const {
		int fd;
		if ((fd = open(path.c_str(), (readonly ? O_RDONLY : O_RDWR) | O_NONBLOCK)) < 0) {
			PERROR("FRONTEND DEVICE");
		}
		return fd;
	}

	int Frontend::open_dmx(const std::string &path) {
		int fd;
		if ((fd = open(path.c_str(), O_RDWR | O_NONBLOCK)) < 0) {
			PERROR("DMX DEVICE");
		}
		return fd;
	}

	int Frontend::open_dvr(const std::string &path) {
		int fd;
		if ((fd = open(path.c_str(), O_RDONLY | O_NONBLOCK)) < 0) {
			PERROR("DVR DEVICE");
		}
		return fd;
	}

	bool Frontend::set_demux_filter(int fd, uint16_t pid) {
		struct dmx_pes_filter_params pesFilter;

		pesFilter.pid      = pid;
		pesFilter.input    = DMX_IN_FRONTEND;
		pesFilter.output   = DMX_OUT_TS_TAP;
		pesFilter.pes_type = DMX_PES_OTHER;
		pesFilter.flags    = DMX_IMMEDIATE_START;

		if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilter) != 0) {
			PERROR("DMX_SET_PES_FILTER");
			return false;
		}
		return true;
	}

	bool Frontend::setFrontendInfo() {
#if SIMU
		sprintf(_fe_info.name, "Simulation DVB-S2/C/T Card");
		_fe_info.frequency_min = 1000000UL;
		_fe_info.frequency_max = 21000000UL;
		_fe_info.symbol_rate_min = 20000UL;
		_fe_info.symbol_rate_max = 250000UL;
#else
		int fd_fe;
		// open frontend in readonly mode
		if ((fd_fe = open_fe(_path_to_fe, true)) < 0) {
			snprintf(_fe_info.name, sizeof(_fe_info.name), "Not Found");
			PERROR("open_fe");
			return false;
		}

		if (ioctl(fd_fe, FE_GET_INFO, &_fe_info) != 0) {
			snprintf(_fe_info.name, sizeof(_fe_info.name), "Not Set");
			PERROR("FE_GET_INFO");
			CLOSE_FD(fd_fe);
			return false;
		}
#endif
		SI_LOG_INFO("Frontend Name: %s", _fe_info.name);

		struct dtv_property dtvProperty;
#if SIMU
		dtvProperty.u.buffer.len = 4;
		dtvProperty.u.buffer.data[0] = SYS_DVBS;
		dtvProperty.u.buffer.data[1] = SYS_DVBS2;
		dtvProperty.u.buffer.data[2] = SYS_DVBT;
#  if FULL_DVB_API_VERSION >= 0x0505
		dtvProperty.u.buffer.data[3] = SYS_DVBC_ANNEX_A;
#  else
		dtvProperty.u.buffer.data[3] = SYS_DVBC_ANNEX_AC;
#  endif
#else
		dtvProperty.cmd = DTV_ENUM_DELSYS;
		dtvProperty.u.data = DTV_UNDEFINED;

		struct dtv_properties dtvProperties;
		dtvProperties.num = 1;       // size
		dtvProperties.props = &dtvProperty;
		if (ioctl(fd_fe, FE_GET_PROPERTY, &dtvProperties ) != 0) {
			// If we are here it can mean we have an DVB-API <= 5.4
			SI_LOG_DEBUG("Unable to enumerate the delivery systems, retrying via old API Call");
			auto index = 0;
			switch (_fe_info.type) {
				case FE_QPSK:
					if (_fe_info.caps & FE_CAN_2G_MODULATION) {
						dtvProperty.u.buffer.data[index] = SYS_DVBS2;
						++index;
					}
					dtvProperty.u.buffer.data[index] = SYS_DVBS;
					++index;
					break;
				case FE_OFDM:
					if (_fe_info.caps & FE_CAN_2G_MODULATION) {
						dtvProperty.u.buffer.data[index] = SYS_DVBT2;
						++index;
					}
					dtvProperty.u.buffer.data[index] = SYS_DVBT;
					++index;
					break;
				case FE_QAM:
#  if FULL_DVB_API_VERSION >= 0x0505
					dtvProperty.u.buffer.data[index] = SYS_DVBC_ANNEX_A;
#  else
					dtvProperty.u.buffer.data[index] = SYS_DVBC_ANNEX_AC;
#  endif
					++index;
					break;
				case FE_ATSC:
					if (_fe_info.caps & (FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO)) {
						dtvProperty.u.buffer.data[index] = SYS_DVBC_ANNEX_B;
						++index;
						break;
					}
				// Fall-through
				default:
					SI_LOG_ERROR("Frontend does not have any known delivery systems");
					CLOSE_FD(fd_fe);
					return false;
			}
			dtvProperty.u.buffer.len = index;
		}
		CLOSE_FD(fd_fe);
#endif
		// clear delsys
		for (std::size_t i = 0; i < MAX_DELSYS; ++i) {
			_info_del_sys[i] = SYS_UNDEFINED;
		}
		// get capability of fe and save it
		_del_sys_size = dtvProperty.u.buffer.len;
		for (std::size_t i = 0; i < dtvProperty.u.buffer.len; i++) {
			switch (dtvProperty.u.buffer.data[i]) {
				case SYS_DSS:
					_info_del_sys[i] = SYS_DSS;
					SI_LOG_DEBUG("Frontend Type: DSS");
					break;
				case SYS_DVBS:
					_info_del_sys[i] = SYS_DVBS;
					SI_LOG_DEBUG("Frontend Type: Satellite (DVB-S)");
					break;
				case SYS_DVBS2:
					_info_del_sys[i] = SYS_DVBS2;
					SI_LOG_DEBUG("Frontend Type: Satellite (DVB-S2)");
					break;
				case SYS_DVBT:
					_info_del_sys[i] = SYS_DVBT;
					SI_LOG_DEBUG("Frontend Type: Terrestrial (DVB-T)");
					break;
				case SYS_DVBT2:
					_info_del_sys[i] = SYS_DVBT2;
					SI_LOG_DEBUG("Frontend Type: Terrestrial (DVB-T2)");
					break;
#if FULL_DVB_API_VERSION >= 0x0505
				case SYS_DVBC_ANNEX_A:
					_info_del_sys[i] = SYS_DVBC_ANNEX_A;
					SI_LOG_DEBUG("Frontend Type: Cable (Annex A)");
					break;
				case SYS_DVBC_ANNEX_C:
					_info_del_sys[i] = SYS_DVBC_ANNEX_C;
					SI_LOG_DEBUG("Frontend Type: Cable (Annex C)");
					break;
#else
				case SYS_DVBC_ANNEX_AC:
					_info_del_sys[i] = SYS_DVBC_ANNEX_AC;
					SI_LOG_DEBUG("Frontend Type: Cable (Annex C)");
					break;
#endif
				case SYS_DVBC_ANNEX_B:
					_info_del_sys[i] = SYS_DVBC_ANNEX_B;
					SI_LOG_DEBUG("Frontend Type: Cable (Annex B)");
					break;
				default:
					_info_del_sys[i] = SYS_UNDEFINED;
					SI_LOG_DEBUG("Frontend Type: Unknown %d", dtvProperty.u.buffer.data[i]);
					break;
			}
		}
		SI_LOG_DEBUG("Frontend Freq: %d Hz to %d Hz", _fe_info.frequency_min, _fe_info.frequency_max);
		SI_LOG_DEBUG("Frontend srat: %d symbols/s to %d symbols/s", _fe_info.symbol_rate_min, _fe_info.symbol_rate_max);

		return true;
	}

	bool Frontend::setProperties(const StreamProperties &properties) {
		struct dtv_property p[15];
		int size = 0;

#define FILL_PROP(CMD, DATA) { p[size].cmd = CMD; p[size].u.data = DATA; ++size; }

		FILL_PROP(DTV_CLEAR, DTV_UNDEFINED);
		switch (properties.getDeliverySystem()) {
			case SYS_DVBT2:
				FILL_PROP(DTV_STREAM_ID,         properties.getUniqueIDPlp());
			// Fall-through
			case SYS_DVBT:
				FILL_PROP(DTV_DELIVERY_SYSTEM,   properties.getDeliverySystem());
				FILL_PROP(DTV_FREQUENCY,         properties.getFrequency() * 1000UL);
				FILL_PROP(DTV_MODULATION,        properties.getModulationType());
				FILL_PROP(DTV_INVERSION,         properties.getSpectralInversion());
				FILL_PROP(DTV_BANDWIDTH_HZ,      properties.getBandwidthHz());
				FILL_PROP(DTV_CODE_RATE_HP,      properties.getFEC());
				FILL_PROP(DTV_CODE_RATE_LP,      properties.getFEC());
				FILL_PROP(DTV_TRANSMISSION_MODE, properties.getTransmissionMode());
				FILL_PROP(DTV_GUARD_INTERVAL,    properties.getGuardInverval());
				FILL_PROP(DTV_HIERARCHY,         properties.getHierarchy());
#if FULL_DVB_API_VERSION >= 0x0509
				FILL_PROP(DTV_LNA,               1);
#endif
				break;
#if FULL_DVB_API_VERSION >= 0x0505
			case SYS_DVBC_ANNEX_A:
			case SYS_DVBC_ANNEX_B:
			case SYS_DVBC_ANNEX_C:
#else
			case SYS_DVBC_ANNEX_AC:
			case SYS_DVBC_ANNEX_B:
#endif
				FILL_PROP(DTV_BANDWIDTH_HZ,      properties.getBandwidthHz());
				FILL_PROP(DTV_DELIVERY_SYSTEM,   properties.getDeliverySystem());
				FILL_PROP(DTV_FREQUENCY,         properties.getFrequency() * 1000UL);
				FILL_PROP(DTV_INVERSION,         properties.getSpectralInversion());
				FILL_PROP(DTV_MODULATION,        properties.getModulationType());
				FILL_PROP(DTV_SYMBOL_RATE,       properties.getSymbolRate());
				FILL_PROP(DTV_INNER_FEC,         properties.getFEC());
				break;

			default:
				return false;
		}

		FILL_PROP(DTV_TUNE, DTV_UNDEFINED);

#undef FILL_PROP

		struct dtv_properties cmdseq;
		cmdseq.num = size;
		cmdseq.props = p;
		// get all pending events to clear the POLLPRI status
		for (;; ) {
			struct dvb_frontend_event dfe;
			if (ioctl(_fd_fe, FE_GET_EVENT, &dfe) == -1) {
				break;
			}
		}
		// set the tuning properties
		if ((ioctl(_fd_fe, FE_SET_PROPERTY, &cmdseq)) == -1) {
			PERROR("FE_SET_PROPERTY failed");
			return false;
		}
		return true;
	}

	bool Frontend::tune(StreamProperties &properties) {
		const int streamID = properties.getStreamID();

		const fe_delivery_system_t delsys = properties.getDeliverySystem();
		if (delsys == SYS_DVBS || delsys == SYS_DVBS2) {
			return _deliverySystem->tune(_fd_fe, properties);
		}

		SI_LOG_DEBUG("Stream: %d, Start tuning process...", streamID);

		// Now tune
		if (!setProperties(properties)) {
			return false;
		}
		return true;
	}

	bool Frontend::setupAndTune(StreamProperties &properties) {
		const int streamID = properties.getStreamID();
		if (!_tuned) {
			// Check if we have already opened a FE
			if (_fd_fe == -1) {
				_fd_fe = open_fe(_path_to_fe, false);
				SI_LOG_INFO("Stream: %d, Opened %s fd: %d", streamID, _path_to_fe.c_str(), _fd_fe);
			}
			// try tuning
			std::size_t timeout = 0;
			while (!tune(properties)) {
				usleep(450000);
				++timeout;
				if (timeout > 3) {
					return false;
				}
			}
			SI_LOG_INFO("Stream: %d, Waiting on lock...", streamID);

			// check if frontend is locked, if not try a few times
			timeout = 0;
			while (timeout < 4) {
				fe_status_t status = FE_TIMEDOUT;
				// first read status
				if (ioctl(_fd_fe, FE_READ_STATUS, &status) == 0) {
					if (status & FE_HAS_LOCK) {
						// We are tuned now
						_tuned = true;
						SI_LOG_INFO("Stream: %d, Tuned and locked (FE status 0x%X)", streamID, status);
						break;
					} else {
						SI_LOG_INFO("Stream: %d, Not locked yet   (FE status 0x%X)...", streamID, status);
					}
				}
				usleep(150000);
				++timeout;
			}
		}
		// Check if we have already a DVR open and are tuned
		if (_fd_dvr == -1 && _tuned) {
			// try opening DVR, try again if fails
			std::size_t timeout = 0;
			while ((_fd_dvr = open_dvr(_path_to_dvr)) == -1) {
				usleep(150000);
				++timeout;
				if (timeout > 3) {
					return false;
				}
			}
			SI_LOG_INFO("Stream: %d, Opened %s fd: %d", streamID, _path_to_dvr.c_str(), _fd_dvr);

			const unsigned long size = properties.getDVRBufferSize();
			if (ioctl(_fd_dvr, DMX_SET_BUFFER_SIZE, size) == -1) {
				PERROR("DMX_SET_BUFFER_SIZE failed");
			}
		}
		return (_fd_dvr != -1) && _tuned;
	}

	void Frontend::resetPid(StreamProperties &properties, int pid) {
		if (properties.getDMXFileDescriptor(pid) != -1 &&
			ioctl(properties.getDMXFileDescriptor(pid), DMX_STOP) != 0) {
			PERROR("DMX_STOP");
		}
		properties.closeDMXFileDescriptor(pid);
		properties.resetPid(pid);
	}

	bool Frontend::updatePIDFilters(StreamProperties &properties) {
		if (properties.hasPIDTableChanged()) {
			properties.resetPIDTableChanged();
			const int streamID = properties.getStreamID();
			SI_LOG_INFO("Stream: %d, Updating PID filters...", streamID);
			int i;
			for (i = 0; i < MAX_PIDS; ++i) {
				// check if PID is used or removed
				if (properties.isPIDUsed(i)) {
					// check if we have no DMX for this PID, then open one
					if (properties.getDMXFileDescriptor(i) == -1) {
						properties.setDMXFileDescriptor(i, open_dmx(_path_to_dmx));
						std::size_t timeout = 0;
						while (set_demux_filter(properties.getDMXFileDescriptor(i), i) != 1) {
							usleep(350000);
							++timeout;
							if (timeout > 3) {
								return false;
							}
						}
						SI_LOG_DEBUG("Stream: %d, Set filter PID: %04d - fd: %03d%s",
								streamID, i, properties.getDMXFileDescriptor(i), properties.isPMT(i) ? " - PMT" : "");
					}
				} else if (properties.getDMXFileDescriptor(i) != -1) {
					// We have a DMX but no PID anymore, so reset it
					SI_LOG_DEBUG("Stream: %d, Remove filter PID: %04d - fd: %03d - Packet Count: %d",
							streamID, i, properties.getDMXFileDescriptor(i), properties.getPacketCounter(i));
					resetPid(properties, i);
				}
			}
		}
		return true;
	}

} // namespace dvb
} // namespace input
