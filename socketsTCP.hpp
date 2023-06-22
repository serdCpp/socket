// work in progress
// TCP socket server & client
// tested by macOS 12.6.6, Window Server 2019
//
// Created by Denis (serdCpp)

#pragma once
#ifndef serd_socketsTCP_h
#define serd_socketsTCP_h

#include <thread>
#include <chrono>
#include <string>
#include <set>
#include <vector>
#include <map>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#else
#error Platform not supported
#endif

namespace serd_lib {

#if defined(__linux__) || defined(__APPLE__)
	typedef int SocketFD;
	typedef ::in_port_t in_port_t;
	typedef ::ssize_t ssize_t;
#elif defined (_WIN32)
	typedef ::SOCKET SocketFD;
	typedef unsigned short in_port_t;
	typedef long ssize_t;
#endif

	constexpr size_t _BUFFER_SIZE = 2048;
	constexpr in_port_t _DEFAULT_PORT = 3487;
	constexpr std::chrono::milliseconds _TIME_BETWEEN_TRIES(500);

#if defined(__linux__) || defined(__APPLE__)
	constexpr int _SOCKET_ERROR = -1;
	constexpr SocketFD _INVALID_SOCKET = -1;
#elif defined (_WIN32)
	constexpr int _SOCKET_ERROR = SOCKET_ERROR;
	constexpr SocketFD _INVALID_SOCKET = INVALID_SOCKET;
	bool _WSAStartup = false;
#endif

	int poll(pollfd* fds, unsigned long nfds, int& timeout) {
#if defined(__linux__) || defined(__APPLE__)
		if (nfds > UINT_MAX) {
			//errno = E2BIG;
			return -1;
		};

		return ::poll(fds, (unsigned int)nfds, timeout);
#elif defined (_WIN32)
		return WSAPoll(fds, nfds, timeout);
#endif
	};

	class Socket {
	private:
		SocketFD sockfd;
		sockaddr_in address;
		std::string preview;
		std::string err;
	private:
		void setPreview() {
			char clientip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(address.sin_addr), clientip, INET_ADDRSTRLEN);
			preview = std::string(clientip) + ":" + std::to_string(ntohs(address.sin_port));
		};
	protected:
		Socket() = delete;
		Socket(const SocketFD& fd) : sockfd(fd), preview("unknown") {}; // lite mode for revc server
		Socket(const sockaddr_in& address, const SocketFD& fd) : address(address), sockfd(fd) {
			if (fd == _INVALID_SOCKET)
				addError("set invalide fd");

			setPreview();
		};
		Socket(const char* address, in_port_t port) : sockfd(_INVALID_SOCKET) {
			memset(&this->address, 0, sizeof(this->address));

			this->address.sin_family = AF_INET;
			this->address.sin_port = htons(port);

			if (inet_pton(AF_INET, address, &this->address.sin_addr) <= 0) {
				addError("Invalid address/ Address not supported");
				return;
			};

#if defined(_WIN32)
			_WSAStartup = _WSAStartup || (WSAStartup(MAKEWORD(2, 2), new WSADATA) == 0);

			if (!_WSAStartup) {
				addError("Error WinSock version initializaion");
				return;
			};
#endif

			if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == _INVALID_SOCKET) {
				addError("Socket creation error");
				return;
			};

			setPreview();
		};
		Socket(const char* address) : Socket(address, _DEFAULT_PORT) {};
		~Socket() {
			close();
		};
	protected:
		void close() {
			if (getSockfd() != _INVALID_SOCKET) {
#if defined(_WIN32)
				closesocket(sockfd);
#else
				::close(sockfd);
#endif
				sockfd = _INVALID_SOCKET;
			};
		};
		inline SocketFD getSockfd() const {
			return sockfd;
		};
		inline std::string toString() const {
			return preview;
		};
		inline sockaddr_in getAddress() const {
			return address;
		};
		inline void addError(const std::string& text) {
#if defined(_WIN32)
			err += (err.size() ? "\n" : "") + text + " #" + std::to_string(WSAGetLastError());
#else
			err += (err.size() ? "\n" : "") + text;
#endif
		};
	public:
		std::string getError() {
			std::string temp = err;
			err.clear();
			return temp;
		};
		inline bool haveError() const {
			return !err.empty();
		};
		inline bool socketIsValid() const {
			return sockfd != _INVALID_SOCKET;
		};
	public:
		inline bool operator== (const Socket& rhs) const {
			return (sockfd != _INVALID_SOCKET
				&& sockfd == rhs.sockfd)
				|| (address.sin_family == rhs.address.sin_family
					&& address.sin_port == rhs.address.sin_port
					&& address.sin_addr.s_addr == rhs.address.sin_addr.s_addr);
		};
		inline bool operator< (const Socket& rhs) const {
			return sockfd < rhs.sockfd
				|| address.sin_family < rhs.address.sin_family
				|| address.sin_port < rhs.address.sin_port
				|| address.sin_addr.s_addr < rhs.address.sin_addr.s_addr;
		};
	};

};

namespace serd {

	class Client : public serd_lib::Socket {
		friend class Server;
	private:
		std::mutex mutexSend;
		bool in_stop = false;
	public:
		Client() = delete;
		Client(const char* address) : Socket(address) {};
		Client(const char* address, serd_lib::in_port_t port) : Socket(address, port) {};
		Client(const sockaddr_in& address, serd_lib::SocketFD& fd) : Socket(address, fd) {};
		~Client() {
			in_stop = true;
		};
	private:
		Client(const serd_lib::SocketFD& fd) : Socket(fd) {}; // lite mode for revc server
	public:
		bool connect() {
			if (haveError()) {
				return false;
			};

			sockaddr_in addr = getAddress();

			if (::connect(getSockfd(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
				addError("Connection Failed");
				return false;
			};

			mutexSend.unlock();

			return true;
		};
		bool recv(std::string& message, const bool& stop) {
			char buffer[serd_lib::_BUFFER_SIZE];
			serd_lib::ssize_t valread;
			pollfd fds;
			int ready;
			unsigned int nfds(1);
			int timeout(std::chrono::milliseconds(serd_lib::_TIME_BETWEEN_TRIES).count());

			message.clear();

			memset(&fds, 0, sizeof(fds));
			fds.fd = getSockfd();
			fds.events = POLLIN;

			while (!stop && !in_stop && socketIsValid()) {
				ready = serd_lib::poll(&fds, nfds, timeout);

				if (ready == 0)
					continue;

				if (ready < 0) {
					addError("poll() failed.");
					close();
					break;
				};

				do {
					valread = ::recv(getSockfd(), buffer, serd_lib::_BUFFER_SIZE - 1, 0);

					if (valread > 0) {
						buffer[valread] = '\0';
						message += buffer;
					} else if (valread == 0) {
						close();
						addError("socket disconnect");
					} else {
						addError("recv failed with error");
					};
				} while (valread == serd_lib::_BUFFER_SIZE - 1);

				break;
			};

			return socketIsValid();
		};
		bool send(const std::string& message) {
			if (!socketIsValid()) {
				addError("Send failed, client disconnected");
				return false;
			};

			mutexSend.lock();
			bool result = ::send(getSockfd(), message.c_str(), message.size(), 0) != serd_lib::_SOCKET_ERROR;
			mutexSend.unlock();

			if (!result)
				addError("Send failed with error");

			return result;
		};
		inline std::string toString() {
			return Socket::toString();
		};
	};

	class Server : public serd_lib::Socket {
	public:
		typedef std::vector<std::pair<Client*, std::string>> ReturnRevc;
	private:
		std::map<serd_lib::SocketFD, Client*> clients;
		std::vector<std::thread> threads;
		bool newClientAccept = false;
		bool in_stop = false;
		std::mutex mutexClients;
	public:
		Server() = delete;
		Server(const Server&) = delete;
		Server(serd_lib::in_port_t port, bool& stop) : Socket((char*)"0.0.0.0", port) {
			sockaddr_in addr = getAddress();
			int opt = 1;
			int addrlen = sizeof(addr);

			if (setsockopt(getSockfd(), SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt))) {
				addError("setsockopt() error.");
				return;
			};

			if (bind(getSockfd(), (struct sockaddr*)&addr, addrlen) < 0) {
				addError("bind() failed.");
				return;
			};

			if (listen(getSockfd(), 3) < 0) {
				addError("listen() failed.");
				return;
			};

			threads.push_back(std::thread([this, &stop]() {this->accept(stop); }));
		};
		Server(bool& stop) : Server(serd_lib::_DEFAULT_PORT, stop) {};
		~Server() {
			in_stop = true;

			for (auto& thread : threads)
				thread.join();

			threads.clear();

			for (auto& [fd, client] : clients)
				delete client;

			clients.clear();
		};
	private:
		void accept(bool& stop) {
			sockaddr_in addr;
			serd_lib::SocketFD clientfd = serd_lib::_INVALID_SOCKET;
			pollfd fds;
			unsigned int nfds(1);
			int timeout(std::chrono::milliseconds(serd_lib::_TIME_BETWEEN_TRIES).count());
			int ready;
			int addrlen = sizeof(addr);

			memset(&fds, 0, sizeof(fds));
			fds.fd = getSockfd();
			fds.events = POLLIN;

			while (!stop && !in_stop && socketIsValid()) {
				ready = serd_lib::poll(&fds, nfds, timeout);

				if (ready == 0)
					continue;

				if (ready < 0) {
					addError("poll() failed.");
					continue;
				};

				clientfd = ::accept(fds.fd, (struct sockaddr*)&addr, (socklen_t*)&addrlen);

				if (clientfd == serd_lib::_INVALID_SOCKET) {
					addError("Accept failed. invalid socket");
					continue;
				};

				if (clients.count(clientfd)) {
					addError("Accept failed. Client (" + clients.at(clientfd)->toString() + ") dublicate.");
					continue;
				};

				Client* client = new Client(addr, clientfd);

				mutexClients.lock();
				clients.insert({ clientfd, client });
				newClientAccept = true;
				mutexClients.unlock();
			};
		};
	public:
		bool send(const std::string& message, const std::set<Client*>& selected, const std::set<Client*>& excluding) {
			std::vector<std::thread> threads;
			std::mutex mutexError;
			std::string error;
			bool usingExcluding = excluding.size();
			bool usingSelected = selected.size();

			auto senderFun = [&error, &mutexError, &message](Client* client) {
				if (!client->send(message)) {
					mutexError.lock();
					error += "sending error (" + client->toString() + ") due to:\n";
					error += client->getError();
					mutexError.unlock();
				};
			};

			for (const auto& [fd, client] : clients) {
				if (usingExcluding && excluding.count(client))
					continue;
				if (usingSelected && !selected.count(client))
					continue;

				threads.push_back(std::thread(senderFun, client));
			};

			for (std::thread& thread : threads)
				thread.join();

			if (error.empty())
				return true;

			addError(error);

			return false;
		};
		bool send(const std::string& message, const std::set<Client*>& selected) {
			return send(message, selected, {});
		};
		bool send(const std::string& message, Client* selected) {
			return send(message, { selected }, {});
		};
		bool send(const std::string& message) {
			return send(message, {}, {});
		};
		bool sendEx(const std::string& message, const std::set<Client*>& excluding) {
			return send(message, {}, excluding);
		};
		bool sendEx(const std::string& message, Client* excluding) {
			return send(message, {}, { excluding });
		};
		bool revc(ReturnRevc& messages, bool& stop, const std::string& disconnectMessage) {
			char buffer[serd_lib::_BUFFER_SIZE];
			serd_lib::ssize_t valread;
			std::string message;

			std::vector<pollfd> fds;
			std::set<serd_lib::SocketFD> fdToDelete;
			int ready;
			int timeout(std::chrono::milliseconds(serd_lib::_TIME_BETWEEN_TRIES).count());
			unsigned long nfds;

			auto refillFds = [this, &fdToDelete, &fds, &messages, &disconnectMessage, &nfds]() {
				fds.clear();

				mutexClients.lock();

				for (const auto& [fd, client] : clients) {
					if (!client->socketIsValid()) {
						if (!disconnectMessage.empty()) {
							messages.push_back({ client, disconnectMessage });
						};

						fdToDelete.insert(fd);
					};

					fds.push_back({ fd, POLLIN });
				};

				newClientAccept = false;
				mutexClients.unlock();

				nfds = fds.size();
			};
			auto clientDelete = [this, &fdToDelete]() {
				if (fdToDelete.empty())
					return;

				mutexClients.lock();
				for (const auto& fd : fdToDelete) {
					if (!clients.count(fd))
						continue;

					delete clients.at(fd);
					clients.erase(fd);
				};
				mutexClients.unlock();

				fdToDelete.clear();
			};

			newClientAccept = true;

			messages.clear();

			while (!stop && !in_stop && socketIsValid() && messages.empty()) {
				if (newClientAccept) {
					refillFds();
					clientDelete();
				};

				if (fds.empty()) {
					std::this_thread::sleep_for(serd_lib::_TIME_BETWEEN_TRIES);
					continue;
				};

				ready = serd_lib::poll(&fds[0], nfds, timeout);

				if (ready == 0) {
					continue;
				};

				if (ready < 0) {
					addError("poll() failed.");
					close();
					return false;
				};

				for (const auto& fd : fds) {
					if (fd.revents == 0)
						continue;

					message.clear();

					do {
						valread = ::recv(fd.fd, buffer, serd_lib::_BUFFER_SIZE - 1, 0);

						if (valread > 0) {
							buffer[valread] = '\0';
							message += buffer;
						} else {
							fdToDelete.insert(fd.fd);
							break;
						};
					} while (valread == serd_lib::_BUFFER_SIZE - 1);

					if (!message.empty()
						&& clients.count(fd.fd)
						&& !fdToDelete.count(fd.fd)) {
						messages.push_back({ clients.at(fd.fd), message });
					};
				};

				clientDelete();
			};

			return true;
		};
	};

};

#endif /* serd_socketsTCP_h */
