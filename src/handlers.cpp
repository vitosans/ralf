/**
* @file handlers.cpp handlers for homestead
*
* Project Clearwater - IMS in the Cloud
* Copyright (C) 2013 Metaswitch Networks Ltd
*
* This program is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation, either version 3 of the License, or (at your
* option) any later version, along with the "Special Exception" for use of
* the program along with SSL, set forth below. This program is distributed
* in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR
* A PARTICULAR PURPOSE. See the GNU General Public License for more
* details. You should have received a copy of the GNU General Public
* License along with this program. If not, see
* <http://www.gnu.org/licenses/>.
*
* The author can be reached by email at clearwater@metaswitch.com or by
* post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
*
* Special Exception
* Metaswitch Networks Ltd grants you permission to copy, modify,
* propagate, and distribute a work formed by combining OpenSSL with The
* Software, or a work derivative of such a combination, even if such
* copying, modification, propagation, or distribution would otherwise
* violate the terms of the GPL. You must comply with the GPL in all
* respects for all of the code used other than OpenSSL.
* "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
* Project and licensed under the OpenSSL Licenses, or a work based on such
* software and licensed under the OpenSSL Licenses.
* "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
* under which the OpenSSL Project distributes the OpenSSL toolkit software,
* as those licenses appear in the file LICENSE-OPENSSL.
*/

#include "handlers.hpp"
#include "message.hpp"
#include "log.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

//LCOV_EXCL_START
// We don't want to actually run the handlers
void PingHandler::run()
{
  _req.add_content("OK");
  _req.send_reply(200);
  delete this;
}

void BillingControllerHandler::run()
{
  if (_req.method() != htp_method_POST)
  {
    _req.send_reply(405);
    return;
  }
  Message* msg = parse_body(call_id(), _req.param("timer-interim"), _req.body());
  if (msg == NULL)
  {
    _req.send_reply(400);
    return;
  }
  _req.send_reply(200);
  _sess_mgr->handle(msg);
  delete this;
}
//LCOV_EXCL_STOP

Message* BillingControllerHandler::parse_body(std::string call_id, std::string timer_param, std::string reqbody)
{
  bool timer_interim = false;
  if (timer_param.compare("true") == 0) {
    timer_interim = true;
  }
  rapidjson::Document* body = new rapidjson::Document();
  std::string bodys = reqbody;
  body->Parse<0>(bodys.c_str());
  std::vector<std::string> ccfs;
  uint32_t session_refresh_time = 0;

  // Verify that the body is correct JSON with an "event" element
  if (!(*body).IsObject() ||
      !(*body).HasMember("event") ||
      !(*body)["event"].IsObject())
  {
    LOG_WARNING("JSON document was either not valid or did not have an 'event' key");
    return NULL;
  }

  // Verify that there is an Accounting-Record-Type and it is one of
  // the four valid types
  if (!((*body)["event"].HasMember("Accounting-Record-Type") &&
        ((*body)["event"]["Accounting-Record-Type"].IsInt())))
  {
    LOG_WARNING("Accounting-Record-Type not available in JSON");
    return NULL;
  }
  Rf::AccountingRecordType record_type((*body)["event"]["Accounting-Record-Type"].GetInt());
  if (!record_type.isValid())
  {
    LOG_ERROR("Accounting-Record-Type was not one of START/INTERIM/STOP/EVENT");
    return NULL;
  }

  // Get the Acct-Interim-Interval if present
  if ((*body)["event"].HasMember("Acct-Interim-Interval") &&
        ((*body)["event"]["Acct-Interim-Interval"].IsInt()))
  {
    session_refresh_time = (*body)["event"]["Acct-Interim-Interval"].GetInt();
  }

  // If we have a START or EVENT Accounting-Record-Type, we must have
  // a list of CCFs to use as peers.

  if (record_type.isStart() || record_type.isEvent())
  {
    if (!((body->HasMember("peers")) && (*body)["peers"].IsObject()))
    {
      LOG_ERROR("JSON lacked a 'peers' object (mandatory for START/EVENT)");
      return NULL;
    }
    if (!((*body)["peers"].HasMember("ccf")) ||(!(*body)["peers"]["ccf"].IsArray()) || ((*body)["peers"]["ccf"].Size() == 0))
    {
      LOG_ERROR("JSON lacked a 'ccf' array, or the array was empty (mandatory for START/EVENT)");
      return NULL;
    }

    for (rapidjson::SizeType i = 0; i < (*body)["peers"]["ccf"].Size(); i++)
    {
      if (!(*body)["peers"]["ccf"][i].IsString())
      {
        LOG_ERROR("JSON contains a 'ccf' array but not all the elements are strings");
        return NULL;
      }
      LOG_DEBUG("Adding CCF %s", (*body)["peers"]["ccf"][i].GetString());
      ccfs.push_back((*body)["peers"]["ccf"][i].GetString());
    }
  }

  Message* msg = new Message(call_id, body, record_type, session_refresh_time, timer_interim);
  if (!ccfs.empty())
  {
    msg->ccfs = ccfs;
  }
  return msg;
}
