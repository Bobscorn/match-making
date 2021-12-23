#include "linux_socket.h"

#include <exception>
#include <stdexcept>
#include <iostream>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>

#include "message.h"

std::string linux_error()
{
	auto err_code = errno;
	std::string error = "Err code: " + std::to_string(err_code) + " (";
	error += strerror(errno);
	return error + ")";
}

std::string linux_error(int err_code)
{
	std::string error = "Err code: " + std::to_string(err_code) + " (";
	error += strerror(errno);
	return error + ")";
}

readable_ip_info convert_to_readable(raw_name_data data)
{
	std::vector<char> buf{ 50, '0', std::allocator<char>() };
	const char* str = inet_ntop(AF_INET, &data.ipv4_addr().sin_addr, buf.data(), buf.size());
	if (!str)
		throw PRINT_MSG_LINE(std::string("Failed to convert sockaddr to string: ") + linux_error());

	std::string address = str;

	std::string port = std::to_string(ntohs(data.ipv4_addr().sin_port));
	readable_ip_info out;
	out.ip_address = address;
	out.port = port;
	return out;
}

LinuxSocket::LinuxSocket(int socket) 
	: _socket(socket)
{ 
	try
	{
		update_name_info();

		if (_address == "Unassigned" || _address.empty() ||
			_port == "Unassigned" || _port.empty()) {
			throw PRINT_MSG_LINE("failed to update name info");
		}
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_LINE);
	}
}

void LinuxSocket::update_name_info()
{
	try
	{
		auto name = get_myname_readable();
		_address = name.ip_address;
		_port = name.port;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_LINE);
	}
}

void LinuxSocket::update_endpoint_info()
{
	try
	{
		auto name = get_peername_readable();
		_endpoint_address = name.ip_address;
		_endpoint_port = name.port;
		_endpoint_assigned = true;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_LINE);
	}
}

void LinuxSocket::update_endpoint_if_needed()
{
	if (!_endpoint_assigned)
	{
		update_endpoint_info();
	}
}

LinuxSocket::~LinuxSocket()
{
	if (_socket >= 0)
	{
		std::cout << "Closing socket: " << _endpoint_address << ":" << _endpoint_port << std::endl;
		close(_socket);
	}
}

void LinuxSocket::shutdown()
{
}

readable_ip_info LinuxSocket::get_peer_data() const
{
	sockaddr_in peer_name;
	socklen_t peer_size = sizeof(peer_name);
	int n = getpeername(_socket, (sockaddr*)&peer_name, &peer_size);
	if (n != 0)
		throw PRINT_MSG_LINE("Failed to getpeername: " + linux_error());

	std::vector<char> buf{ 50, '0', std::allocator<char>() };
	const char* str = inet_ntop(AF_INET, &peer_name.sin_addr, buf.data(), buf.size());
	if (!str)
		throw PRINT_MSG_LINE(std::string("Failed to convert sockaddr to string: ") + linux_error());

	std::string address = str;

	std::string port = std::to_string(peer_name.sin_port);
	readable_ip_info out;
	out.ip_address = address;
	out.port = port;
	return out;
}

raw_name_data LinuxSocket::get_peername_raw() const
{
	sockaddr_in peer_name;
	socklen_t peer_size = sizeof(peer_name);
	int n = getpeername(_socket, (sockaddr*)&peer_name, &peer_size);
	if (n != 0)
		throw PRINT_MSG_LINE(std::string("[Socket] Failed to getpeername with: ") + linux_error());

	raw_name_data raw_data;
	raw_data.name = *(sockaddr*)&peer_name;
	raw_data.name_len = peer_size;
	return raw_data;
}

raw_name_data LinuxSocket::get_myname_raw() const
{
	sockaddr_in peer_name;
	socklen_t peer_size = sizeof(peer_name);
	int n = getsockname(_socket, (sockaddr*)&peer_name, &peer_size);
	if (n != 0)
		throw PRINT_MSG_LINE(std::string("[Socket] Failed to getsockname with: ") + linux_error());

	raw_name_data raw_data;
	raw_data.name = *(sockaddr*)&peer_name;
	raw_data.name_len = peer_size;
	return raw_data;
}

readable_ip_info LinuxSocket::get_peername_readable() const 
{
	try
	{
		return convert_to_readable(get_peername_raw());
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_LINE);
	}
}

readable_ip_info LinuxSocket::get_myname_readable() const
{
	try
	{
		return convert_to_readable(get_myname_raw());
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_LINE);
	}
}

int listen_construct(std::string port)
{
	try
	{
		std::cout << "[Listen] Create new Socket on port (with localhost): " << port << std::endl;
		int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		socklen_t cli_len;
		if (listen_socket < 0)
			throw std::runtime_error(std::string("[Listen] Failed to create linux socket: ") + linux_error());

		// BEGIN POTENTIAL BUG FIX TEST
		int reuseVal = 1;
		int n = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuseVal, sizeof(reuseVal));
		if (n < 0)
		{
			auto err = linux_error();
			close(listen_socket);
			throw std::runtime_error(std::string("[Listen] Failed to set socket SO_REUSEADDR (bug testing) with: ") + err);
		}
#ifdef SO_REUSEPORT
		n = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT, &reuseVal, sizeof(reuseVal));
		if (n < 0)
		{
			auto err = linux_error();
			close(listen_socket);
			throw std::runtime_error(std::string("[Listen] Failed to set socket SO_REUSEPORT (bug testing) with: ") + err);
		}
#endif
		// END BUG FIX TEST

		struct sockaddr_in serv_addr, cli_addr;

		int portno = atoi(port.c_str());
		bzero((char*)&serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(portno);

		std::cout << "Binding..." << std::endl;
		if (bind(listen_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
			throw std::runtime_error(std::string("[Listen] Failed to bind linux socket: ") + linux_error());

		return listen_socket;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to construct linux listen socket"));
	}
}

linux_listen_socket::linux_listen_socket(std::string port) : LinuxSocket(listen_construct(port))
{
}

void linux_listen_socket::listen()
{
	std::cout << "[Listen] Socket now Listening (" << get_my_ip() << ":" << get_my_port() << ")" << std::endl;
	if (::listen(_socket, 5) < 0)
		throw PRINT_MSG_LINE(std::string("[Listen] Error when listening: ") + linux_error());
}

bool linux_listen_socket::has_connection()
{
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	fd_set poll_read_set;
	FD_ZERO(&poll_read_set);
	FD_SET(_socket, &poll_read_set);

	pollfd poll_thing;
	poll_thing.fd = _socket;
	poll_thing.events = POLLRDNORM;
	poll_thing.revents = 0;

	int n = poll(&poll_thing, 1, 0);
	if (n > 0)
		return poll_thing.revents | POLLRDNORM;
	if (n < 0)
		throw PRINT_MSG_LINE(std::string("[Listen] Failed to poll linux socket readability: ") + linux_error());
	return false;
}

std::unique_ptr<IDataSocket> linux_listen_socket::accept_connection()
{
	try
	{
		std::cout << "[Listen] Socket Attempting to accept a connection" << std::endl;
		sockaddr_in client_addr;
		socklen_t client_len;
		int new_socket = accept(_socket, (struct sockaddr*)&client_addr, &client_len);
		if (new_socket < 0)
			return nullptr;

		raw_name_data name;
		name.name = *(sockaddr*)&client_addr;
		name.name_len = client_len;
		auto readable = convert_to_readable(name);
		std::cout << "[Listen] Accepted a connection: " << readable.ip_address << ":" << readable.port << std::endl;
		return std::make_unique<linux_data_socket>(new_socket);
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to accept connection"));
	}
}

int steal_construct(std::unique_ptr<IReusableNonBlockingConnectSocket>&& old)
{
	try
	{
		std::cout << "[Data] Moving linux_reusable_nonblocking_connection_socket " << old->get_identifier_str() << " to a data_socket" << std::endl;
		linux_reuse_nonblock_connection_socket& real_old = *dynamic_cast<linux_reuse_nonblock_connection_socket*>(old.get());
		int flags = fcntl(real_old.get_socket(), F_GETFL);
		if (flags == -1)
			throw std::runtime_error(std::string("Failed to query socket's flags: ") + linux_error());
		int n = fcntl(real_old.get_socket(), F_SETFL, flags & (~O_NONBLOCK));
		if (n == -1)
			throw std::runtime_error(std::string("Failed to set socket as blocking again: ") + linux_error());
		int conn_socket = real_old.get_socket();
		real_old.clear_socket();
		old = nullptr;
		return conn_socket;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to move data from reusable non blocking connect socket"));
	}
}

linux_data_socket::linux_data_socket(std::unique_ptr<IReusableNonBlockingConnectSocket>&& old) : LinuxSocket(steal_construct(std::move(old)))
{
	update_endpoint_info();
}

linux_data_socket::linux_data_socket(int socket) : LinuxSocket(socket)
{
	try
	{
		std::cout << "[Data] Copy Constructor Data socket" << std::endl;
		update_endpoint_info();
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_LINE);
	}
}

int data_connect_cosntruct(std::string peer_address, std::string peer_port)
{
	try
	{
		std::cout << "[Data] Creating a Linux Data Socket connecting to: " << peer_address << ":" << peer_port << std::endl;

		struct addrinfo* result = NULL,
			* ptr = NULL,
			hints;

		bzero(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		int n = getaddrinfo(peer_address.c_str(), peer_port.c_str(), &hints, &result);

		if (n < 0)
			throw PRINT_MSG_LINE("Failed to get address info for: " + peer_address + ":" + peer_port + " with: " + linux_error());

		int conn_socket = -1;
		for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
		{
			conn_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
			if (conn_socket < 0)
			{
				auto last_err = linux_error();
				freeaddrinfo(result);
				throw PRINT_MSG_LINE("[Data] Failed to create data socket with: " + last_err);
			}

			// BEGIN POTENTIAL BUG FIX TEST
			int reuseVal = 1;
			int n = setsockopt(conn_socket, SOL_SOCKET, SO_REUSEADDR, &reuseVal, sizeof(reuseVal));
			if (n < 0)
			{
				auto err = linux_error();
				close(conn_socket);
				throw PRINT_MSG_LINE("[Data] Failed to set socket SO_REUSEADDR (bug testing) with: " + err);
			}
#ifdef SO_REUSEPORT
			n = setsockopt(conn_socket, SOL_SOCKET, SO_REUSEPORT, &reuseVal, sizeof(reuseVal));
			if (n < 0)
			{
				auto err = linux_error();
				close(conn_socket);
				throw PRINT_MSG_LINE("[Data] Failed to set socket SO_REUSEPORT (bug testing) with: " + err);
			}
#endif
			// END BUG FIX TEST

			n = connect(conn_socket, ptr->ai_addr, ptr->ai_addrlen);
			if (n < 0)
			{
				close(conn_socket);
				conn_socket = -1;
				continue;
			}
			auto readable = convert_to_readable(raw_name_data{ *ptr->ai_addr, ptr->ai_addrlen });
			std::cout << "[Data] Successfully connected to: " << readable.ip_address << ":" << readable.port << std::endl;
			break;
		}

		freeaddrinfo(result);

		if (conn_socket < 0)
			throw PRINT_MSG_LINE("[Data] No sockets successfully connected to peer");

		return conn_socket;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to construct linuxdatasocket"));
	}
}

linux_data_socket::linux_data_socket(std::string peer_address, std::string peer_port) : LinuxSocket(data_connect_cosntruct(peer_address, peer_port))
{
	update_endpoint_info();
}

std::vector<char> linux_data_socket::receive_message()
{
	try
	{
		std::cout << "[Data] Receiving data from Socket: (" << get_my_ip() << ":" << get_my_port() << ", " << get_endpoint_ip() << ":" << get_endpoint_port() << ") (priv, pub)" << std::endl;
		std::vector<char> recv_data{ 500, '0', std::allocator<char>() };
		int n = read(_socket, recv_data.data(), 500);
		if (n < -1)
		{
			std::cerr << "[Data] Failed to read data from linux socket: " << linux_error() << std::endl;
			return std::vector<char>();
		}
		recv_data.resize(n);
		log_msg(recv_data, false, *this);
		_seen_data += n;
		return recv_data;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to receive data with linux data socket"));
	}
}

bool linux_data_socket::has_message()
{
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	fd_set poll_read_set;
	FD_ZERO(&poll_read_set);
	FD_SET(_socket, &poll_read_set);

	int n = select(_socket + 1, &poll_read_set, 0, 0, &timeout);
	if (n < 0)
		throw PRINT_MSG_LINE("[Data] Failed to poll linux socket readability: " + linux_error());

	return n > 0;
}

bool linux_data_socket::send_data(const std::vector<char>& data)
{
	try
	{
		log_msg(data, true, *this);
		std::cout << "Sending " << data.size() << " bytes to: " << "(" << get_my_ip() << ":" << get_my_port() << ", " << get_endpoint_ip() << " : " << get_endpoint_port() << ") (priv, pub)" << std::endl;
		int n = write(_socket, data.data(), data.size());
		if (n < 0)
		{
			std::cerr << "Error sending data: " << linux_error() << std::endl;
			return false;
		}
		_sent_bytes += data.size();
		return true;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to send data with linux data socket"));
	}
}

bool linux_data_socket::has_died()
{
	try
	{
		if (has_message())
		{
			std::vector<char> recv_data{ 100, '0', std::allocator<char>() };
			int n = recv(_socket, recv_data.data(), recv_data.size(), MSG_PEEK);
			if (n < -1)
			{
				std::cerr << "[Data] Failed to peek data from linux socket (trying to determine if closed): " << linux_error() << std::endl;
				return true;
			}
			return n == 0;
		}
		return false;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to determine linux data socket died"));
	}
}

int reuse_listen_construct(std::string port)
{
	try
	{
		std::cout << "[ListenReuseNoB] Creating Reusable Listen Socket on (localhost): " << port << std::endl;

		int portno = atoi(port.c_str());

		struct sockaddr_in serv_addr;
		memset(&serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(portno);

		int listen_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
		if (listen_socket < 0)
			throw std::runtime_error(std::string("[ListenReuseNoB] (localhost:") + port + ") Failed to create reusable nonblocking listen socket: " + linux_error());

		int reuseVal = 1;
		int n = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuseVal, sizeof(reuseVal));
		if (n < 0)
		{
			auto err = linux_error();
			close(listen_socket);
			throw std::runtime_error(std::string("[ListenReuseNoB] (localhost:") + port + ") Failed to set socket SO_REUSEADDR: " + err);
		}
#ifdef SO_REUSEPORT
		n = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT, &reuseVal, sizeof(reuseVal));
		if (n < 0)
		{
			auto err = linux_error();
			close(listen_socket);
			throw std::runtime_error(std::string("[ListenReuseNoB] (localhost:") + port + ") Failed to set socket SO_REUSEPORT: " + err);
		}
#endif

		n = ::bind(listen_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
		if (n < 0)
		{
			auto err = linux_error();
			close(listen_socket);
			throw std::runtime_error(std::string("[ListenReuseNoB] (localhost:") + port + ") Failed to bind reuseable nonblocking socket : " + err);
		}

		return listen_socket;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("NonBlock Listen Socket (on port: " + port + " Failed with : "));
	}
}

linux_reuse_nonblock_listen_socket::linux_reuse_nonblock_listen_socket(std::string port) : LinuxSocket(reuse_listen_construct(port))
{
	
}

void linux_reuse_nonblock_listen_socket::listen()
{
	std::cout << "[ListenReuseNoB] Now Listening on: " << get_my_ip() << ":" << get_my_port() << std::endl;
	auto n = ::listen(_socket, 4);
	if (n < 1 && n != EINPROGRESS && n != EAGAIN)
	{
		auto err = errno;
		if (err && err != EINPROGRESS && err != EAGAIN)
			throw PRINT_MSG_LINE("[ListenReuseNoB] Failed to listen with: " + linux_error());
	}
}

bool linux_reuse_nonblock_listen_socket::has_connection()
{
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	fd_set poll_read_set;
	FD_ZERO(&poll_read_set);
	FD_SET(_socket, &poll_read_set);

	int n = select(_socket + 1, &poll_read_set, 0, 0, &timeout);
	if (n < 0)
		throw PRINT_MSG_LINE("[ListenReuseNoB] Failed to poll linux socket readability (has connection): " + linux_error());

	return n > 0;
}

std::unique_ptr<IDataSocket> linux_reuse_nonblock_listen_socket::accept_connection()
{
	try
	{
		std::cout << "[ListenReuseNoB] Accepting Connection..." << std::endl;
		sockaddr_in client_addr;
		socklen_t client_len;
		int accepted_socket = accept(_socket, (struct sockaddr*)&client_addr, &client_len);

		if (accepted_socket < 0)
			return nullptr;
		raw_name_data name;
		name.name = *(sockaddr*)&client_addr;
		name.name_len = client_len;
		auto readable = convert_to_readable(name);
		std::cout << "[ListenReuseNoB] Accepted Connection from: " << readable.ip_address << ":" << readable.port << std::endl;
		return std::make_unique<linux_data_socket>(accepted_socket);
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to accept connection with linux listen socket"));
	}
}

int reuse_connection_construct(raw_name_data data)
{
	try
	{
		auto readable = convert_to_readable(data);
		std::cout << "[DataReuseNoB] Creating Connection socket to: " << readable.ip_address << ":" << readable.port << std::endl;
		int conn_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
		if (conn_socket < 0)
		{
			auto err = linux_error();
			throw std::runtime_error(std::string("[DataReuseNoB] Failed to create nonblocking socket: ") + err);
		}

		int reuseVal = 1;
		int n = setsockopt(conn_socket, SOL_SOCKET, SO_REUSEADDR, &reuseVal, sizeof(reuseVal));
		if (n < 0)
		{
			auto err = linux_error();
			close(conn_socket);
			throw std::runtime_error(std::string("[DataReuseNoB] (") + readable.ip_address + ":" + readable.port + ") Failed to set socket SO_REUSEADDR with: " + err);
		}
		reuseVal = 1;
#ifdef SO_REUSEPORT
		n = setsockopt(conn_socket, SOL_SOCKET, SO_REUSEPORT, &reuseVal, sizeof(reuseVal));
		if (n < 0)
		{
			auto err = linux_error();
			close(conn_socket);
			throw std::runtime_error(std::string("[DataReuseNoB] (") + readable.ip_address + ":" + readable.port + ") Failed to set socket SO_REUSEPORT with: " + err);
		}
#endif

		n = ::bind(conn_socket, &data.name, data.name_len);
		if (n < 0)
		{
			auto err = linux_error();
			close(conn_socket);
			throw std::runtime_error(std::string("[DataReuseNoB] (") + readable.ip_address + ":" + readable.port + ") Failed to bind connect socket with: " + err);
		}
		std::cout << "[DataReuseNoB] Successfully bound Data socket to: " << readable.ip_address << ":" << readable.port << std::endl;

		return conn_socket;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to construct linux connection socket"));
	}
}

linux_reuse_nonblock_connection_socket::linux_reuse_nonblock_connection_socket(raw_name_data data, std::string ip_address, std::string port) : LinuxSocket(reuse_connection_construct(data))
{
	connect(ip_address, port);
}

void linux_reuse_nonblock_connection_socket::connect(std::string ip_address, std::string port)
{
	try
	{
		std::cout << "[DataReuseNoB] Trying to connect to: " << ip_address << ":" << port << std::endl;
		struct addrinfo* results, hints;
		bzero(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		int iResult = 0;

		iResult = getaddrinfo(ip_address.c_str(), port.c_str(), &hints, &results);
		if (iResult != 0)
			throw std::runtime_error((std::string("Failed to getaddrinfo, error: ") + std::to_string(iResult)).c_str());

		if (results == nullptr)
			throw std::runtime_error((std::string("No possible sockets found for '") + ip_address + ":" + port + "'").c_str());

		iResult = ::connect(_socket, results->ai_addr, (int)results->ai_addrlen);
		if (iResult < 0)
		{
			auto last_err = errno;
			if (last_err != EAGAIN && last_err != EINPROGRESS)
				throw std::runtime_error(std::string("Failed when attempting to connect to '") + ip_address + ":" + port + "' with err: " + linux_error(last_err));
		}
		std::cout << "[DataReuseNoB] Successfully BEGUN Connection to: " << ip_address << ":" << port << std::endl;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to connect with linux connection socket"));
	}
}

ConnectionStatus linux_reuse_nonblock_connection_socket::has_connected()
{
	try
	{
		if (_socket < 0)
			return ConnectionStatus::FAILED;

		fd_set write_set;
		FD_ZERO(&write_set);
		FD_SET(_socket, &write_set);

		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;

		int n = select(_socket + 1, NULL, &write_set, NULL, &timeout);

		if (n < 0)
			throw std::runtime_error(std::string("Failed to select nonblock connect socket write-ability (whether it has connected): ") + linux_error());

		if (FD_ISSET(_socket, &write_set))
		{
			update_endpoint_if_needed();
			return ConnectionStatus::SUCCESS;
		}

		fd_set except_set;
		FD_ZERO(&except_set);
		FD_SET(_socket, &except_set);
		n = select(_socket + 1, NULL, NULL, &except_set, &timeout);
		if (n < 0)
			throw std::runtime_error(std::string("Failed to select socket error status with: ") + linux_error());
		if (!FD_ISSET(_socket, &except_set))
			return ConnectionStatus::PENDING;

		int sock_error = 0;
		socklen_t sock_error_size = sizeof(sock_error);
		if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, (char*)&sock_error, &sock_error_size) < 0)
			throw std::runtime_error(std::string("Failed to get socket error code with: ") + linux_error());

		std::cerr << "Socket has error code: " << sock_error << std::endl;

		return ConnectionStatus::FAILED;
	}
	catch (...)
	{
		std::throw_with_nested(PRINT_MSG_LINE("failed to determine linux connection socket had connected"));
	}
}