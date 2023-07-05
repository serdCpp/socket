// TCP socket server & client
// tested by macOS 12.6.6, Window Server 2019
//
// Created by Denis (serdCpp)
// -std=c++17

#pragma once
#ifndef serd_socketsTCP_h
#define serd_socketsTCP_h

#include <thread>
#include <chrono>
#include <string>
#include <set>
#include <vector>
#include <map>
#include <mutex>

#if defined(__linux__) || defined(__APPLE__)
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <poll.h>
	#include <unistd.h>
	#include <climits>
	#include <cstring>
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
	unsigned long _WSAStartup = 0;
	bool _WSACleanup = true;
#endif

	int poll(pollfd* fds, unsigned long nfds, int& timeout) {
#if defined(__linux__) || defined(__APPLE__)
		if (nfds > UINT_MAX) {
			return -1;
		};

		return ::poll(fds, (unsigned int)nfds, timeout);
#elif defined (_WIN32)
		return WSAPoll(fds, nfds, timeout);
#endif
	};

	class Socket {
	public:
		std::string getError() {
			std::string temp = mErr;
			mErr.clear();
			return temp;
		};
		inline bool haveError() const {
			return !mErr.empty();
		};
		inline bool socketIsValid() const {
			return mSockfd != _INVALID_SOCKET;
		};
		inline std::string toString() const {
			return mPreview;
		};
	protected:
		Socket() = delete;
		Socket(const sockaddr_in& address, const SocketFD& fd) : mAddress(address), mSockfd(fd) {
			if (fd == _INVALID_SOCKET)
				addError("set invalide fd");

			setPreview();
#if defined(_WIN32)
			WSAStartup();
#endif
		};
		Socket(const char* address, const in_port_t& port) : mSockfd(_INVALID_SOCKET) {
			memset(&mAddress, 0, sizeof(mAddress));

			mAddress.sin_family = AF_INET;
			mAddress.sin_port = htons(port);

			if (inet_pton(AF_INET, address, &mAddress.sin_addr) <= 0) {
				addError("Invalid address/ Address not supported");
				return;
			};

#if defined(_WIN32)
			WSAStartup();
#endif

			if ((mSockfd = socket(AF_INET, SOCK_STREAM, 0)) == _INVALID_SOCKET) {
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
				closesocket(mSockfd);
#else
				::close(mSockfd);
#endif
				mSockfd = _INVALID_SOCKET;
			};

#if defined(_WIN32)
			WSACleanup();
#endif
		};
		inline SocketFD getSockfd() const {
			return mSockfd;
		};
		inline sockaddr_in getAddress() const {
			return mAddress;
		};
		inline void addError(const std::string& text) {
#if defined(_WIN32)
			mErr += (mErr.size() ? "\n" : "") + text + " #" + std::to_string(WSAGetLastError());
#else
			mErr += (mErr.size() ? "\n" : "") + text;
#endif
		};
	private:
		SocketFD mSockfd;
		sockaddr_in mAddress;
		std::string mPreview;
		std::string mErr;
	private:
		void setPreview() {
			char clientip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(mAddress.sin_addr), clientip, INET_ADDRSTRLEN);
			mPreview = std::string(clientip) + ":" + std::to_string(ntohs(mAddress.sin_port));
		};
#if defined(_WIN32)
		inline bool WSAStartup() {
			if (_WSAStartup == 0 && (::WSAStartup(MAKEWORD(2, 2), new WSADATA) != 0)) {
				addError("Error WinSock version initializaion");
				return false;
			};

			++_WSAStartup;
			return true;
		};
		inline void WSACleanup() {
			if (_WSACleanup && --_WSAStartup == 0)
				::WSACleanup();
		};
#endif
	};


};

namespace serd {

	class Client : public serd_lib::Socket {
	public:
		Client() = delete;
		Client(const char* address) : Socket(address) {};
		Client(const char* address, serd_lib::in_port_t port) : Socket(address, port) {};
		Client(const sockaddr_in& address, const serd_lib::SocketFD& fd) : Socket(address, fd) {};
		~Client() {};
	private:
		std::mutex mMutexSend;
	public:
		bool connect() {
			if (haveError())
				return false;

			sockaddr_in addr = getAddress();

			if (::connect(getSockfd(), (struct sockaddr*)&addr, sizeof(addr)) < 0)
				addError("Connection Failed");

			return !haveError();
		};
		bool recv(std::string& message, const bool& stopFlag) {
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

			while (!stopFlag && socketIsValid()) {
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

			mMutexSend.lock();
			bool result = ::send(getSockfd(), message.c_str(), message.size(), 0) != serd_lib::_SOCKET_ERROR;
			mMutexSend.unlock();

			if (!result)
				addError("Send failed with error");

			return result;
		};
	};

	class Server : public serd_lib::Socket {
	public:
		typedef std::vector<std::pair<Client*, std::string>> ReturnRevc;
	public:
		Server(const Server&) = delete;
		Server(serd_lib::in_port_t port) : Socket((char*)"0.0.0.0", port) {
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
			
			mThread = std::thread([this]() {this->thrAccept(); });
		};
		Server() : Server(serd_lib::_DEFAULT_PORT) {};
		~Server() {
			mStop = true;
			
			if(mThread.joinable())
				mThread.join();
		};
	public:
		bool send(const std::string& message, const std::set<Client*>& selected = {}, const std::set<Client*>& excluding = {}) {
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

			for (const auto& [fd, client] : mClients) {
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
		bool revc(ReturnRevc& messages, bool& stop, const std::string& disconnectMessage) {
			int ready;
			int timeout(std::chrono::milliseconds(serd_lib::_TIME_BETWEEN_TRIES).count());
			
			std::vector<pollfd> fds;
			std::vector<serd_lib::SocketFD> fdToDelete;
			std::vector<std::thread> threads;
			
			messages.clear();
			
			auto revcFun = [this, &messages, &fdToDelete](const serd_lib::SocketFD fd) -> void {
				char buffer[serd_lib::_BUFFER_SIZE];
				std::string message;
				serd_lib::ssize_t valread;
				
				do {
					valread = ::recv(fd, buffer, serd_lib::_BUFFER_SIZE - 1, 0);

					if (valread > 0) {
						buffer[valread] = '\0';
						message += buffer;
					} else {
						fdToDelete.push_back(fd);
						break;
					};
				} while (valread == serd_lib::_BUFFER_SIZE - 1);

				if (!message.empty()) {
					messages.push_back({ this->mClients.at(fd), message });
				};
			};

			while (socketIsValid() && !stop && !mStop && messages.empty()) {
				fds = mClients.getPollVector();
				
				if (fds.empty()) {
					std::this_thread::sleep_for(serd_lib::_TIME_BETWEEN_TRIES);
					continue;
				};

				ready = serd_lib::poll(&fds[0], static_cast<unsigned long>(fds.size()), timeout);

				if (ready == 0) {
					continue;
				} else if (ready < 0) {
					addError("poll() failed.");
					close();
					return false;
				};

				for (const auto& element : fds) {
					if (element.revents != 0)
						threads.push_back(std::thread(revcFun, element.fd));
				};
				
				for(auto& thread : threads)
					thread.join();
				
				threads.clear();
				
				mClients.deleteFds(fdToDelete);
				fdToDelete.clear();
			};

			return true;
		};
	private:
		class Clients {
			typedef std::map<serd_lib::SocketFD, Client*> MapType;
		public:
			void add(const sockaddr_in addr, const serd_lib::SocketFD fd) {
				if(fd == serd_lib::_INVALID_SOCKET || mMap.count(fd) > 0)
					return;
				
				mMutex.lock();
				mMap.insert({ fd, new Client(addr, fd) });
				mNeedRebuild = true;
				mMutex.unlock();
			};
			void deleteFds(const std::vector<serd_lib::SocketFD> fds) {
				mMutex.lock();
				deleteFdsWithoutMutex(fds);
				mMutex.unlock();
			};
			std::vector<pollfd> getPollVector() {
				if(mNeedRebuild) {
					std::vector<serd_lib::SocketFD> fdToDelete;
					mMutex.lock();
					mPollVector.clear();
					
					for (const auto& [fd, client] : mMap) {
						if (!client->socketIsValid())
							fdToDelete.push_back(fd);
						else
							mPollVector.push_back({ fd, POLLIN });
					};
					
					deleteFdsWithoutMutex(fdToDelete);
					
					mNeedRebuild = false;
					
					mMutex.unlock();
				};
				
				return mPollVector;
			};
		public:
			serd_lib::ssize_t count(serd_lib::SocketFD fd) {
				return mMap.count(fd);
			};
			Client* at(serd_lib::SocketFD fd) {
				return mMap.at(fd);
			};
		public:
			inline MapType::iterator begin() {
				return mMap.begin();
			};
			inline MapType::iterator end() {
				return mMap.end();
			};
			inline MapType::const_iterator begin() const {
				return mMap.begin();
			};
			inline MapType::const_iterator end() const {
				return mMap.end();
			};
		public:
			~Clients() {
				mMutex.lock();
				
				for(auto& [fd, client] : mMap) {
					delete client;
				};
				
				mMap.clear();
				mMutex.unlock();
			};
		private:
			MapType mMap;
			std::mutex mMutex;
			std::vector<pollfd> mPollVector;
			bool mNeedRebuild = false;
			//std::vector<std::string> mPrevDisconnected;
		private:
			void deleteFdsWithoutMutex(const std::vector<serd_lib::SocketFD> fds) {
				Client* client = nullptr;
				
				for(const auto& fd : fds) {
					if (count(fd)) {
						client = at(fd);
						//mPrevDisconnected.push_back(client->toString());
						delete client;
						mMap.erase(fd);
					};
				};
				
				mNeedRebuild = true;
			};
		} mClients;
		bool mStop = false;
		std::thread mThread;
	private:
		void thrAccept() {
			sockaddr_in addr;
			serd_lib::SocketFD clientfd = serd_lib::_INVALID_SOCKET;
			pollfd fds;
			int timeout(std::chrono::milliseconds(serd_lib::_TIME_BETWEEN_TRIES).count());
			int ready;
			int addrlen = sizeof(addr);

			memset(&fds, 0, sizeof(fds));
			fds.fd = getSockfd();
			fds.events = POLLIN;

			while (!mStop && socketIsValid()) {
				ready = serd_lib::poll(&fds, static_cast<unsigned int>(1), timeout);

				if (ready == 0)
					continue;
				else if (ready < 0) {
					addError("poll() failed.");
					continue;
				};

				clientfd = ::accept(fds.fd, (struct sockaddr*)&addr, (socklen_t*)&addrlen);

				if (clientfd == serd_lib::_INVALID_SOCKET)
					addError("Accept failed. invalid socket");
				else if (mClients.count(clientfd))
					addError("Accept failed. Client (" + mClients.at(clientfd)->toString() + ") dublicate.");
				else
					mClients.add(addr, clientfd);
			};
		};
	};
};

#endif /* serd_socketsTCP_h */
