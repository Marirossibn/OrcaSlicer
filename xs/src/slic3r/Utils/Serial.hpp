#ifndef slic3r_GUI_Utils_Serial_hpp_
#define slic3r_GUI_Utils_Serial_hpp_

#include <vector>
#include <string>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>


namespace Slic3r {
namespace Utils {

struct SerialPortInfo {
	std::string port;
	std::string hardware_id;
	std::string friendly_name;
	bool 		is_printer = false;
};

inline bool operator==(const SerialPortInfo &sp1, const SerialPortInfo &sp2)
{
	return sp1.port 	   == sp2.port 	      &&
		   sp1.hardware_id == sp2.hardware_id &&
		   sp1.is_printer  == sp2.is_printer;
}

extern std::vector<std::string> 	scan_serial_ports();
extern std::vector<SerialPortInfo> 	scan_serial_ports_extended();


class Serial : public boost::asio::serial_port
{
public:
	Serial(boost::asio::io_service &io_service);
	// This c-tor opens the port for communication with a printer - it sets a baud rate and calls printer_reset()
	Serial(boost::asio::io_service &io_service, const std::string &name, unsigned baud_rate);
	Serial(const Serial &) = delete;
	Serial &operator=(const Serial &) = delete;
	~Serial();

	void set_baud_rate(unsigned baud_rate);
	void set_DTR(bool on);

	// Resets the line number both internally as well as with the firmware using M110
	void reset_line_num();

	// Reads a line or times out, the timeout is in milliseconds
	bool read_line(unsigned timeout, std::string &line, boost::system::error_code &ec);

	// Perform setup for communicating with a printer
	// Sets a baud rate and calls printer_reset()
	void printer_setup(unsigned baud_rate);

	// Write data from a string
	size_t write_string(const std::string &str);
	
	bool printer_ready_wait(unsigned retries, unsigned timeout);

	// Write Marlin-formatted line, with a line number and a checksum
	size_t printer_write_line(const std::string &line, unsigned line_num);

	// Same as above, but with internally-managed line number
	size_t printer_write_line(const std::string &line);
	
	// Toggles DTR to reset the printer
	void printer_reset();

	// Formats a line Marlin-style, ie. with a sequential number and a checksum
	static std::string printer_format_line(const std::string &line, unsigned line_num);
private:
	unsigned m_line_num = 0;
};


} // Utils
} // Slic3r

#endif /* slic3r_GUI_Utils_Serial_hpp_ */
