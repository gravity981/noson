/*
 *      Copyright (C) 2015 Jean-Luc Barriere
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 3, or (at your option)
 *  any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "eventbroker.h"
#include "wsstatus.h"
#include "tinyxml2.h"
#include "cppdef.h"
#include "debug.h"

#include <cstdio>

#define NS_RCS "urn:schemas-upnp-org:metadata-1-0/RCS/"
#define NS_AVT "urn:schemas-upnp-org:metadata-1-0/AVT/"

using namespace NSROOT;

EventBroker::EventBroker(EventHandler::EventHandlerThread* handler, SHARED_PTR<TcpSocket>& sockPtr)
: m_handler(handler)
, m_sockPtr(sockPtr)
, m_buffer(NULL)
{
}

EventBroker::~EventBroker()
{
  SAFE_DELETE_ARRAY(m_buffer);
}


void EventBroker::Process()
{
  if (!m_handler || !m_sockPtr || !m_sockPtr->IsConnected())
    return;

  struct timeval socket_timeout = { 0, 500000 };
  WSRequestBroker rb(m_sockPtr.get(), socket_timeout);
  std::string resp;

  if (!rb.IsParsed())
  {
    WSStatus status(HSC_Bad_Request);
    resp.append(REQUEST_PROTOCOL " ").append(status.GetString()).append(" ").append(status.GetMessage());
    resp.append("\r\n\r\n");
    m_sockPtr->SendMessage(resp.c_str(), resp.size());
    m_sockPtr->Disconnect();
    return;
  }

  // Check for request NOTIFY
  if (rb.GetParsedMethod() == HRM_NOTIFY &&
          rb.GetParsedNamedEntry("NT") == "upnp:event" &&
          rb.GetParsedNamedEntry("CONTENT-TYPE").compare(0, 8, "text/xml") == 0 &&
          rb.HasContent())
  {
    EventMessage msg;
    msg.event = EVENT_UPNP_PROPCHANGE;
    msg.subject.push_back(rb.GetParsedNamedEntry("SID"));
    msg.subject.push_back(rb.GetParsedNamedEntry("SEQ"));
    size_t len = rb.GetContentLength();
    m_buffer = new char[len + 1];
    m_buffer[len] = '\0';
    char* pos = m_buffer;
    char* end = m_buffer + len;
    size_t l = 0;
    while ((l = rb.ReadContent(pos, end - pos)))
      pos += l;
    // Parse xml content
    tinyxml2::XMLDocument rootdoc;
    if (rootdoc.Parse(m_buffer, len) != tinyxml2::XML_SUCCESS)
    {
      DBG(DBG_ERROR, "%s: parse xml failed\n", __FUNCTION__);
      WSStatus status(HSC_Internal_Server_Error);
      resp.append(REQUEST_PROTOCOL " ").append(status.GetString()).append(" ").append(status.GetMessage());
      resp.append("\r\n\r\n");
      m_sockPtr->SendMessage(resp.c_str(), resp.size());
      m_sockPtr->Disconnect();
      return;
    }
    tinyxml2::XMLElement* root; // root element
    tinyxml2::XMLElement* elem; // an element
    tinyxml2::XMLNode* node;    // a node
    tinyxml2::XMLDocument doc;  // a document
    const char* str;

    if ((root = rootdoc.RootElement()) && memcmp(root->Name(), "e:propertyset", 13) == 0)
    {
      // Check for embedded doc 'Event': propertyset/property/LastChange
      if ((node = root->FirstChild()) && memcmp(node->Value(), "e:property", 10) == 0 &&
              (elem = node->FirstChildElement("LastChange")))
      {
        if (doc.Parse(elem->GetText()) != tinyxml2::XML_SUCCESS ||
              !(elem = doc.RootElement()) ||
              !(str = elem->Attribute("xmlns")))
        {
          DBG(DBG_ERROR, "%s: invalid or not supported content\n", __FUNCTION__);
          DBG(DBG_ERROR, "%s: dump => %s\n", __FUNCTION__, m_buffer);
          WSStatus status(HSC_Internal_Server_Error);
          resp.append(REQUEST_PROTOCOL " ").append(status.GetString()).append(" ").append(status.GetMessage());
          resp.append("\r\n\r\n");
          m_sockPtr->SendMessage(resp.c_str(), resp.size());
          m_sockPtr->Disconnect();
          return;
        }

        if (memcmp(str, NS_RCS, sizeof(NS_RCS)) == 0 && (node = elem->FirstChildElement("InstanceID")))
        {
          msg.subject.push_back("RCS");
          elem = node->FirstChildElement(NULL);
          while (elem)
          {
            std::string name(elem->Name());
            if ((str = elem->Attribute("channel")))
              name.append("/").append(str);
            msg.subject.push_back(name);
            if ((str = elem->Attribute("val")))
              msg.subject.push_back(str);
            else
              msg.subject.push_back("");
            DBG(DBG_PROTO, "%s: %s = %s\n", __FUNCTION__, name.c_str(), str);
            elem = elem->NextSiblingElement(NULL);
          }
        }
        else if (memcmp(str, NS_AVT, sizeof(NS_AVT)) == 0 && (node = elem->FirstChildElement("InstanceID")))
        {
          msg.subject.push_back("AVT");
          elem = node->FirstChildElement(NULL);
          while (elem)
          {
            std::string name(elem->Name());
            msg.subject.push_back(name);
            if ((str = elem->Attribute("val")))
              msg.subject.push_back(str);
            else
              msg.subject.push_back("");
            DBG(DBG_PROTO, "%s: %s = %s\n", __FUNCTION__, name.c_str(), str);
            elem = elem->NextSiblingElement(NULL);
          }
        }
        else
          DBG(DBG_WARN, "%s: not supported content (%s)\n", __FUNCTION__, str);
      }
      // Check for propertyset/property/
      else
      {
        msg.subject.push_back("PROPERTY");
        node = root->FirstChildElement("e:property");
        while (node)
        {
          if ((elem = node->FirstChildElement(NULL)))
          {
            std::string name(elem->Name());
            msg.subject.push_back(name);
            if ((str = elem->GetText()))
              msg.subject.push_back(str);
            else
              msg.subject.push_back("");
            DBG(DBG_PROTO, "%s: %s = %s\n", __FUNCTION__, name.c_str(), str);
          }
          node = node->NextSibling();
        }
      }
    }
    else
    {
      DBG(DBG_ERROR, "%s: invalid or not supported content\n", __FUNCTION__);
      DBG(DBG_ERROR, "%s: dump => %s\n", __FUNCTION__, m_buffer);
      WSStatus status(HSC_Internal_Server_Error);
      resp.append(REQUEST_PROTOCOL " ").append(status.GetString()).append(" ").append(status.GetMessage());
      resp.append("\r\n\r\n");
      m_sockPtr->SendMessage(resp.c_str(), resp.size());
      m_sockPtr->Disconnect();
      return;
    }

    m_handler->DispatchEvent(msg);
    WSStatus status(HSC_OK);
    resp.append(REQUEST_PROTOCOL " ").append(status.GetString()).append(" ").append(status.GetMessage());
    resp.append("\r\n\r\n");
    m_sockPtr->SendMessage(resp.c_str(), resp.size());
    m_sockPtr->Disconnect();
    return;
  }

  // Check for others request
  WSStatus status(HSC_Internal_Server_Error);
  switch (rb.GetParsedMethod())
  {
    case HRM_HEAD:
    case HRM_GET:
    {
      EventMessage msg;
      msg.event = EVENT_UNKNOWN;
      msg.subject.push_back("GET");
      msg.subject.push_back(rb.GetParsedURI());
      m_handler->DispatchEvent(msg);
      status.Set(HSC_OK);
      break;
    }
    default:
      status.Set(HSC_Internal_Server_Error);
  }
  resp.append(REQUEST_PROTOCOL " ").append(status.GetString()).append(" ").append(status.GetMessage());
  resp.append("\r\n\r\n");
  m_sockPtr->SendMessage(resp.c_str(), resp.size());
  m_sockPtr->Disconnect();
}
