#include "serial.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <stdexcept>

Serial::Serial(const std::string &name, int baudrate) {
	mFile = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    	if (mFile < 0)
		throw std::runtime_error("Failed to open serial");

	try {
		struct termios tty;
		if (::tcgetattr(mFile, &tty) != 0)
			throw std::runtime_error("tcgetattr failed");

		::cfsetospeed(&tty, baudrate);
		::cfsetispeed(&tty, baudrate);

		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8 bits
		tty.c_iflag &= ~IGNBRK; // ignore break
		tty.c_lflag = 0;
		tty.c_oflag = 0;
		tty.c_cc[VMIN] = 0; // non-blocking
		tty.c_cc[VTIME] = 1; // read timeout
		tty.c_iflag &= ~(IXON | IXOFF | IXANY); // disable xon/xoff ctrl
		tty.c_cflag |= (CLOCAL | CREAD); // ignore modem controls, enable reading
		tty.c_cflag &= ~(PARENB | PARODD); // disable parity
		tty.c_cflag &= ~CSTOPB;
		tty.c_cflag &= ~CRTSCTS;

		if (::tcsetattr(mFile, TCSANOW, &tty) != 0)
			throw std::runtime_error("tcsetattr failed");

	} catch(const std::exception &e) {
		::close(mFile);
		throw std::runtime_error(std::string("Failed to configure serial: ") + e.what());
	}
}
 
Serial::~Serial() {
	::close(mFile);
}

void Serial::write(const std::string &str) {
	::write(mFile, str.data(), str.size());
}

