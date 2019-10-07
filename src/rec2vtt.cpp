/*
 * Copyright (C) 2019  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{0};
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("rec")) || (0 == commandlineArguments.count("odvd")) ) {
        std::cerr << argv[0] << " extracts the content from a given .rec file using a provided .odvd message specification into a .vtt file." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " --rec=<Recording from an OD4Session> --odvd=<ODVD Message Specification>" << std::endl;
        std::cerr << "Example: " << argv[0] << " --rec=myRecording.rec --odvd=myMessages.odvd" << std::endl;
        retCode = 1;
    }
    else {
        cluon::MessageParser mp;
        std::pair<std::vector<cluon::MetaMessage>, cluon::MessageParser::MessageParserErrorCodes> messageParserResult;
        {
            std::ifstream fin(commandlineArguments["odvd"], std::ios::in|std::ios::binary);
            if (fin.good()) {
                std::string input(static_cast<std::stringstream const&>(std::stringstream() << fin.rdbuf()).str()); // NOLINT
                fin.close();
                messageParserResult = mp.parse(input);
                std::clog << "Found " << messageParserResult.first.size() << " messages." << std::endl;
            }
            else {
                std::cerr << "[rec2vtt]: Message specification '" << commandlineArguments["odvd"] << "' not found." << std::endl;
                return retCode = 1;
            }
        }

        std::fstream fin(commandlineArguments["rec"], std::ios::in|std::ios::binary);
        if (fin.good()) {
            fin.close();

            std::map<int32_t, cluon::MetaMessage> scope;
            for (const auto &e : messageParserResult.first) { scope[e.messageIdentifier()] = e; }

            constexpr bool AUTOREWIND{false};
            constexpr bool THREADING{false};
            cluon::Player player(commandlineArguments["rec"], AUTOREWIND, THREADING);

            uint32_t envelopeCounter{0};
            int32_t oldPercentage = -1;
            cluon::data::TimeStamp previousTimeStamp;
            cluon::data::TimeStamp previousTimer;
            cluon::data::TimeStamp timer;
            std::cout << "WEBVTT" << std::endl << std::endl;
            while (player.hasMoreData()) {
                auto next = player.getNextEnvelopeToBeReplayed();
                if (next.first) {
                    {
                        envelopeCounter++;
                        const int32_t percentage = static_cast<int32_t>((static_cast<float>(envelopeCounter)*100.0f)/static_cast<float>(player.totalNumberOfEnvelopesInRecFile()));
                        if ( (percentage % 5 == 0) && (percentage != oldPercentage) ) {
                            std::cerr << "[rec2vtt]: Processed " << percentage << "%." << std::endl;
                            oldPercentage = percentage;
                        }
                    }
                    cluon::data::Envelope env{std::move(next.second)};
                    if (scope.count(env.dataType()) > 0) {
                        cluon::FromProtoVisitor protoDecoder;
                        std::stringstream sstr(env.serializedData());
                        protoDecoder.decodeFrom(sstr);

                        cluon::MetaMessage m = scope[env.dataType()];
                        cluon::GenericMessage gm;
                        gm.createFrom(m, messageParserResult.first);
                        gm.accept(protoDecoder);

                        std::stringstream sstrKey;
                        sstrKey << env.dataType() << "/" << env.senderStamp();
                        const std::string KEY = sstrKey.str();

//                        std::cout << KEY << " " << env.sent().seconds() << ", " << env.sent().microseconds() << std::endl;

                        if (env.dataType() == 1046) {
                            if (0 == previousTimeStamp.seconds()) {
                                previousTimeStamp = env.sent();
                            }
                            cluon::data::TimeStamp now = env.sent();
                            int64_t delta = cluon::time::deltaInMicroseconds(now, previousTimeStamp);
                            previousTimeStamp = now;

                            if (!(delta > 10*1000)) {
                                continue;
                            }

                            timer = cluon::time::fromMicroseconds(cluon::time::toMicroseconds(timer) + delta);

                            std::stringstream sstr_cout;
                            sstr_cout.precision(10);
                            gm.accept([](uint32_t, const std::string &, const std::string &) {},
                                       [&sstr_cout](uint32_t, std::string &&, std::string &&n, auto v) { sstr_cout << n << " = " << v << '\n'; },
                                       []() {});

                            {
                                int64_t secs = (previousTimer.seconds()%60);
                                int64_t mins = (secs/60);
                                int64_t hours = (mins/60);
                                std::cout << std::setw(2) << std::setfill('0') << hours << ":"
                                          << std::setw(2) << std::setfill('0') << mins << ":"
                                          << std::setw(2) << std::setfill('0') << secs << ".";
                                std::cout << std::setw(3) << std::setfill('0') << (previousTimer.microseconds()/1000);
                            }
                            std::cout << " --> ";
                            {
                                int64_t secs = (timer.seconds()%60);
                                int64_t mins = (secs/60);
                                int64_t hours = (mins/60);
                                std::cout << std::setw(2) << std::setfill('0') << hours << ":"
                                          << std::setw(2) << std::setfill('0') << mins << ":"
                                          << std::setw(2) << std::setfill('0') << secs << ".";
                                std::cout << std::setw(3) << std::setfill('0') << (timer.microseconds()/1000);
                            }
                            std::cout << '\n';
                            {
                                std::time_t t = static_cast<std::time_t>(env.sent().seconds());
                                std::cout << std::put_time(std::localtime(&t), "%Y-%m-%d %X") << "." << std::setw(3) << std::setfill('0') << (env.sent().microseconds()/1000) << ": ";
                            }
                            std::cout << sstr_cout.str() << '\n';

                            previousTimer = timer;
                        }
                    }
                }
            }
        }
        else {
            std::cerr << "[rec2vtt]: Recording '" << commandlineArguments["rec"] << "' not found." << std::endl;
            retCode = 1;
        }
    }
    return retCode;
}

