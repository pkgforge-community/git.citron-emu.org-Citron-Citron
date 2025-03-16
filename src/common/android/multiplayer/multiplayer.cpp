// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/android/id_cache.h"
#include "multiplayer.h"

#include "common/android/android_common.h"

#include "core/core.h"
#include "network/network.h"
#include "android/log.h"


#include <thread>
#include <chrono>

namespace IDCache = Common::Android;
Network::RoomNetwork* room_network;

void AddNetPlayMessage(jint type, jstring msg) {
    IDCache::GetEnvForThread()->CallStaticVoidMethod(IDCache::GetNativeLibraryClass(),
                                                     IDCache::GetAddNetPlayMessage(), type, msg);
}

void AddNetPlayMessage(int type, const std::string& msg) {
    JNIEnv* env = IDCache::GetEnvForThread();
    AddNetPlayMessage(type, Common::Android::ToJString(env, msg));
}

void ClearChat() {
    IDCache::GetEnvForThread()->CallStaticVoidMethod(IDCache::GetNativeLibraryClass(),
                                                     IDCache::ClearChat());
}


bool NetworkInit(Network::RoomNetwork* room_network_) {
    room_network = room_network_;
    bool result = room_network->Init();

    if (!result) {
        return false;
    }

    if (auto member = room_network->GetRoomMember().lock()) {
        // register the network structs to use in slots and signals
        member->BindOnStateChanged([](const Network::RoomMember::State& state) {
            if (state == Network::RoomMember::State::Joined ||
                state == Network::RoomMember::State::Moderator) {
                NetPlayStatus status;
                std::string msg;
                switch (state) {
                    case Network::RoomMember::State::Joined:
                        status = NetPlayStatus::ROOM_JOINED;
                        break;
                    case Network::RoomMember::State::Moderator:
                        status = NetPlayStatus::ROOM_MODERATOR;
                        break;
                    default:
                        return;
                }
                AddNetPlayMessage(static_cast<int>(status), msg);
            }
        });
        member->BindOnError([](const Network::RoomMember::Error& error) {
            NetPlayStatus status;
            std::string msg;
            switch (error) {
                case Network::RoomMember::Error::LostConnection:
                    status = NetPlayStatus::LOST_CONNECTION;
                    break;
                case Network::RoomMember::Error::HostKicked:
                    status = NetPlayStatus::HOST_KICKED;
                    break;
                case Network::RoomMember::Error::UnknownError:
                    status = NetPlayStatus::UNKNOWN_ERROR;
                    break;
                case Network::RoomMember::Error::NameCollision:
                    status = NetPlayStatus::NAME_COLLISION;
                    break;
                case Network::RoomMember::Error::IpCollision:
                    status = NetPlayStatus::MAC_COLLISION;
                    break;
                case Network::RoomMember::Error::WrongVersion:
                    status = NetPlayStatus::WRONG_VERSION;
                    break;
                case Network::RoomMember::Error::WrongPassword:
                    status = NetPlayStatus::WRONG_PASSWORD;
                    break;
                case Network::RoomMember::Error::CouldNotConnect:
                    status = NetPlayStatus::COULD_NOT_CONNECT;
                    break;
                case Network::RoomMember::Error::RoomIsFull:
                    status = NetPlayStatus::ROOM_IS_FULL;
                    break;
                case Network::RoomMember::Error::HostBanned:
                    status = NetPlayStatus::HOST_BANNED;
                    break;
                case Network::RoomMember::Error::PermissionDenied:
                    status = NetPlayStatus::PERMISSION_DENIED;
                    break;
                case Network::RoomMember::Error::NoSuchUser:
                    status = NetPlayStatus::NO_SUCH_USER;
                    break;
            }
            AddNetPlayMessage(static_cast<int>(status), msg);
        });
        member->BindOnStatusMessageReceived([](const Network::StatusMessageEntry& status_message) {
            NetPlayStatus status = NetPlayStatus::NO_ERROR;
            std::string msg(status_message.nickname);
            switch (status_message.type) {
                case Network::IdMemberJoin:
                    status = NetPlayStatus::MEMBER_JOIN;
                    break;
                case Network::IdMemberLeave:
                    status = NetPlayStatus::MEMBER_LEAVE;
                    break;
                case Network::IdMemberKicked:
                    status = NetPlayStatus::MEMBER_KICKED;
                    break;
                case Network::IdMemberBanned:
                    status = NetPlayStatus::MEMBER_BANNED;
                    break;
                case Network::IdAddressUnbanned:
                    status = NetPlayStatus::ADDRESS_UNBANNED;
                    break;
            }
            AddNetPlayMessage(static_cast<int>(status), msg);
        });
        member->BindOnChatMessageReceived([](const Network::ChatEntry& chat) {
            NetPlayStatus status = NetPlayStatus::CHAT_MESSAGE;
            std::string msg(chat.nickname);
            msg += ": ";
            msg += chat.message;
            AddNetPlayMessage(static_cast<int>(status), msg);
        });
    }

    return true;
}
NetPlayStatus NetPlayCreateRoom(const std::string& ipaddress, int port,
                              const std::string& username, const std::string& password,
                              const std::string& room_name, int max_players) {

    __android_log_print(ANDROID_LOG_INFO, "NetPlay", "NetPlayCreateRoom called with ipaddress: %s, port: %d, username: %s, room_name: %s, max_players: %d", ipaddress.c_str(), port, username.c_str(), room_name.c_str(), max_players);

    auto member = room_network->GetRoomMember().lock();
    if (!member) {
        return NetPlayStatus::NETWORK_ERROR;
    }

    if (member->GetState() == Network::RoomMember::State::Joining || member->IsConnected()) {
        return NetPlayStatus::ALREADY_IN_ROOM;
    }

    auto room = room_network->GetRoom().lock();
    if (!room) {
        return NetPlayStatus::NETWORK_ERROR;
    }

    if (room_name.length() < 3 || room_name.length() > 20) {
        return NetPlayStatus::CREATE_ROOM_ERROR;
    }

    // Placeholder game info
    const AnnounceMultiplayerRoom::GameInfo game{
            .name = "Default Game",
            .id = 0,  // Default program ID
    };

     port = (port == 0) ? Network::DefaultRoomPort : static_cast<u16>(port);

    if (!room->Create(room_name, "", ipaddress, static_cast<u16>(port), password,
                      static_cast<u32>(std::min(max_players, 16)), username, game, nullptr, {})) {
        return NetPlayStatus::CREATE_ROOM_ERROR;
    }

    // Failsafe timer to avoid joining before creation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    member->Join(username, ipaddress.c_str(), static_cast<u16>(port), 0, Network::NoPreferredIP, password, "");

    // Failsafe timer to avoid joining before creation
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (member->GetState() == Network::RoomMember::State::Joined ||
            member->GetState() == Network::RoomMember::State::Moderator) {
            return NetPlayStatus::NO_ERROR;
        }
    }

    // If join failed while room is created, clean up the room
    room->Destroy();
    return NetPlayStatus::CREATE_ROOM_ERROR;
}

NetPlayStatus NetPlayJoinRoom(const std::string& ipaddress, int port,
                            const std::string& username, const std::string& password) {
    auto member = room_network->GetRoomMember().lock();
    if (!member) {
        return NetPlayStatus::NETWORK_ERROR;
    }

    port =
            (port == 0) ? Network::DefaultRoomPort : static_cast<u16>(port);


    if (member->GetState() == Network::RoomMember::State::Joining || member->IsConnected()) {
        return NetPlayStatus::ALREADY_IN_ROOM;
    }

    member->Join(username, ipaddress.c_str(), static_cast<u16>(port), 0, Network::NoPreferredIP, password, "");

    // Wait a bit for the connection and join process to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (member->GetState() == Network::RoomMember::State::Joined ||
        member->GetState() == Network::RoomMember::State::Moderator) {
        return NetPlayStatus::NO_ERROR;
    }

    if (!member->IsConnected()) {
        return NetPlayStatus::COULD_NOT_CONNECT;
    }

    return NetPlayStatus::WRONG_PASSWORD;
}

void NetPlaySendMessage(const std::string& msg) {
    if (auto room = room_network->GetRoomMember().lock()) {
        if (room->GetState() != Network::RoomMember::State::Joined &&
            room->GetState() != Network::RoomMember::State::Moderator) {

            return;
        }
        room->SendChatMessage(msg);
    }
}

void NetPlayKickUser(const std::string& username) {
    if (auto room = room_network->GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&username](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == username;
                               });
        if (it != members.end()) {
            room->SendModerationRequest(Network::RoomMessageTypes::IdModKick, username);
        }
    }
}

void NetPlayBanUser(const std::string& username) {
    if (auto room = room_network->GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&username](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == username;
                               });
        if (it != members.end()) {
            room->SendModerationRequest(Network::RoomMessageTypes::IdModBan, username);
        }
    }
}

void NetPlayUnbanUser(const std::string& username) {
    if (auto room = room_network->GetRoomMember().lock()) {
        room->SendModerationRequest(Network::RoomMessageTypes::IdModUnban, username);
    }
}

std::vector<std::string> NetPlayRoomInfo() {
    std::vector<std::string> info_list;
    if (auto room = room_network->GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        if (!members.empty()) {
            // name and max players
            auto room_info = room->GetRoomInformation();
            info_list.push_back(room_info.name + "|" + std::to_string(room_info.member_slots));
            // all members
            for (const auto& member : members) {
                info_list.push_back(member.nickname);
            }
        }
    }
    return info_list;
}

bool NetPlayIsJoined() {
    auto member = room_network->GetRoomMember().lock();
    if (!member) {
        return false;
    }

    return (member->GetState() == Network::RoomMember::State::Joined ||
            member->GetState() == Network::RoomMember::State::Moderator);
}

bool NetPlayIsHostedRoom() {
    if (auto room = room_network->GetRoom().lock()) {
        return room->GetState() == Network::Room::State::Open;
    }
    return false;
}

void NetPlayLeaveRoom() {
    if (auto room = room_network->GetRoom().lock()) {
        // if you are in a room, leave it
        if (auto member = room_network->GetRoomMember().lock()) {
            member->Leave();
        }

        ClearChat();

        // if you are hosting a room, also stop hosting
        if (room->GetState() == Network::Room::State::Open) {
            room->Destroy();
        }
    }
}

void NetworkShutdown() {
    room_network->Shutdown();
}

bool NetPlayIsModerator() {
    auto member = room_network->GetRoomMember().lock();
    if (!member) {
        return false;
    }
    return member->GetState() == Network::RoomMember::State::Moderator;
}

std::vector<std::string> NetPlayGetBanList() {
    std::vector<std::string> ban_list;
    if (auto room = room_network->GetRoom().lock()) {
        auto [username_bans, ip_bans] = room->GetBanList();

        // Add username bans
        for (const auto& username : username_bans) {
            ban_list.push_back(username);
        }

        // Add IP bans
        for (const auto& ip : ip_bans) {
            ban_list.push_back(ip);
        }
    }
    return ban_list;
}
