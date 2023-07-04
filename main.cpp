// work in progress
// exemple console UI for TCP socket server & client
// tested by macOS 12.6.6
//
// Created by Denis (serdCpp)
// -std=c++17

#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <mutex>

#include "socketsTCP.hpp"

std::mutex mutexCout;

void showMessage(const std::string&, const bool&);
int consoleChat(std::vector<std::string>);

int main(int argc, const char* argv[]) {

	std::vector<std::string> exCommand;

	if (argc > 1) {
		for (int i = 1; i < argc; ++i)
			exCommand.push_back(argv[i]);
	};

	return consoleChat(exCommand);

};

void showMessage(const std::string& str, const bool& newline = true) {
	mutexCout.lock();
	std::cout << "\r    \r" << str;
	
	if(newline)
		std::cout << "\nyou:";
	
	std::cout << std::flush;
		
	mutexCout.unlock();
};

int consoleChat(std::vector<std::string> exCommand) {
	struct Input {
		std::string user;
		std::string prev_user;
		serd::Client* client = nullptr;
		serd::Server* server = nullptr;
		bool (*callBack)(Input&) = nullptr;
		bool exit = false;
		std::thread thread;

		~Input() {
			close();
		};
		void close(bool exit = true) {
			this->exit = true;
			
			if (thread.joinable())
				thread.join();

			if (server != nullptr) {
				delete server;
				server = nullptr;
			};

			if (client != nullptr) {
				delete client;
				client = nullptr;
			};
			
			this->exit = exit;
		};
	} input;
	std::string command;
	std::map<std::string, bool (*)(Input&)> menu;

	if (exCommand.empty()) {
		showMessage("--programm start--");
		exCommand.push_back("-help");
	};

	menu["help"] = [](Input& input) -> bool {
		const std::string defPort(std::to_string(serd_lib::_DEFAULT_PORT));
		struct : public std::string {
			inline bool operator +=(const std::string& rhs) {
				std::string* ptr = this;
				*ptr+= (ptr->size() ? "\n" : "") + rhs;
				return true;
			};
		} str;
		
		str+= "command start at '-'";
		str+= "help   - get information.";
		str+= "exit   - close programm.";
		str+= "stop   - stop client or server.";
		str+= "client - connect to chat server.";
		str+= "         Exemple:\"-client 192.168.50.204:3487\"";
		str+= "         where 192.168.50.204 - server adrress";
		str+= "         3487 - server port (default " + defPort + ").";
		str+= "server - start chat server.\n";
		str+= "         where 3487 - server port (default " + defPort + ").";
		
		showMessage(str);

		return true;
	};
	menu["exit"] = [](Input& input) -> bool {
		input.close();
		return true;
	};
	menu["stop"] = [](Input& input) -> bool {
		input.close(false);
		return true;
	};
	menu["client"] = [](Input& input) -> bool {
		if (input.server != nullptr) {
			showMessage("Client not avalible, because server started.");
			showMessage("Stop server first");
			return false;
		}
		else if (input.client != nullptr) {
			showMessage("The client is already running.");
			return false;
		};

		std::string addr;
		std::string port;
		std::string* cur = &addr;

		for (const char& ch : input.user) {
			if (ch == ':') {
				cur = &port;
				continue;
			}
			else if (ch == ' ') {
				break;
			};

			*cur += ch;
		};

		if (port.empty()) {
			input.client = new serd::Client((char*)addr.c_str());
		}
		else {
			input.client = new serd::Client((char*)addr.c_str(), std::stoi(port));
		};

		if (!input.client->connect()) {
			showMessage(input.client->getError());
			delete input.client;
			input.client = nullptr;
			return false;
		};

		input.callBack = [](Input& input) -> bool {
			return input.client->send(input.user);
		};

		auto threadRecv = [&input]() {
			std::string message;

			while (!input.exit && input.client->recv(message, input.exit)) {
				if (!message.empty())
					showMessage(message);
			};

			if (!input.exit && input.client->socketIsValid())
				showMessage(input.client->getError());
			else
				showMessage("disconnect " + input.client->toString());

			delete input.client;
			input.client = nullptr;
			input.callBack = nullptr;
		};

		input.thread = std::thread(threadRecv);
		
		showMessage("ready...");

		return true;
	};
	menu["server"] = [](Input& input) -> bool {
		if (input.server != nullptr) {
			showMessage("The server is already running.");
			return false;
		}
		else if (input.client != nullptr) {
			showMessage("Client started.\nServer not avalible.\nStop client first");
			return false;
		};

		if (input.user.empty()) {
			input.server = new serd::Server();
		}
		else {
			input.server = new serd::Server(std::stoi(input.user));
		};

		if (input.server->haveError()) {
			showMessage("Server don't create.");
			showMessage(input.server->getError());
			delete input.server;
			input.server = nullptr;
			return false;
		};

		input.callBack = [](Input& input) -> bool {
			return input.server->send(input.server->serd_lib::Socket::toString() + ":" + input.user);
		};

		auto threadRecv = [&input]() {
			serd::Server::ReturnRevc messages;

			while (!input.exit && input.server->revc(messages, input.exit, "disconnect")) {
				for (const auto& [client, message] : messages) {
					std::string str = client->toString() + ":" + message;

					showMessage(str);

					input.server->send(str, {}, {client});
				};
			};

			if (!input.exit && input.server->socketIsValid())
				showMessage(input.server->getError());
			else
				showMessage("server stop");

			delete input.server;
			input.server = nullptr;
			input.callBack = nullptr;
		};

		input.thread = std::thread(threadRecv);
		
		showMessage("ready...");

		return true;
	};

	while (!input.exit) {
		if (exCommand.empty()) {
			showMessage("you:", false);
			
			if(!std::getline(std::cin, input.user)) {
				showMessage("Error cin");
				std::cin.clear();
				std::cin.sync();
				continue;
			};
		} else {
			input.user = exCommand[0];
			exCommand.erase(exCommand.begin());
			showMessage("preset:" + input.user);
		};

		if (input.user.empty()) {
			continue;
		};

		if (input.user[0] != '-') {
			if (input.callBack == nullptr)
				showMessage("connect or start the server first");
			else
				input.callBack(input);
			continue;
		};

		command.clear();

		for (const char& ch : input.user) {
			if (ch == '-')
				continue;
			else if (ch == '=')
				break;
			else
				command += ch;
		};

		input.user = input.user.erase(0, command.size() + 2);

		if (menu.count(command))
			menu[command](input);
		else
			showMessage("command not found");
	};

	return 0;
};
