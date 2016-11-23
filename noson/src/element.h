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

#ifndef ELEMENT_H
#define	ELEMENT_H

#include <local_config.h>
#include "sharedptr.h"

#include <string>
#include <vector>
#include <cstring>

namespace NSROOT
{

  class Element : public std::string
  {
  public:
    explicit Element(const std::string& key) : m_key(key) {}
    explicit Element(const std::string& key, const std::string& value) : std::string(value) , m_key(key) {}
    Element(const Element& _other) : std::string(_other), m_key(_other.m_key), m_attrs(_other.m_attrs) {}
    Element& operator =(const Element& _other) { m_key = _other.m_key; m_attrs = _other.m_attrs; this->assign(_other); return *this; }
    virtual ~Element() {}

    static const std::string& Nil()
    {
      static const std::string nil("");
      return nil;
    }

    std::string XML(std::string ns = std::string()) const
    {
      std::string ret;
      if (!ns.empty())
        ns.append(":");
      ret.append("<").append(ns).append(m_key);
      for (std::vector<Element>::const_iterator it = m_attrs.begin(); it != m_attrs.end(); ++it)
        ret.append(" ").append(it->m_key).append("=\"").append(it->XMLEncoded()).append("\"");
      ret.append(">").append(XMLEncoded()).append("</").append(ns).append(m_key).append(">");
      return ret;
    }

    const std::string& GetKey() const { return m_key; }

    void SetAttribut(const Element& var)
    {
      for (std::vector<Element>::iterator it = m_attrs.begin(); it != m_attrs.end(); ++it)
        if (it->m_key == var.m_key)
        {
          *it = var;
          return;
        }
      m_attrs.push_back(var);
    }

    void SetAttribut(const std::string& name, const std::string& value)
    {
      SetAttribut(Element(name, value));
    }

    const std::string& GetAttribut(const std::string& name) const
    {
      for (std::vector<Element>::const_iterator it = m_attrs.begin(); it != m_attrs.end(); ++it)
        if (it->m_key == name)
          return *it;
      return Nil();
    }

    std::vector<Element>& Attributs() { return m_attrs; }

    std::string XMLEncoded() const
    {
      std::string ret;
      ret.reserve(size());
      for (std::string::const_iterator it = begin(); it != end(); ++it)
      {
        if (*it == '&')
          ret.append("&amp;");
        else if (*it == '<')
          ret.append("&lt;");
        else if (*it == '>')
          ret.append("&gt;");
        else if (*it == '"')
          ret.append("&quot;");
        else
          ret.push_back(*it);
      }
      return ret;
    }

    // compare prefix of a qualified element name
    static bool XMLPrefixEqual(const char* qname, const char* prefix)
    {
      unsigned n = 0;
      const char* p = qname;
      while (*p != '\0')
        if (*(++p) == ':')
        {
          n = p - qname;
          break;
        }
      return (strlen(prefix) == n && strncmp(qname, prefix, n) == 0);
    }

    // compare name of qualified element name
    static bool XMLNameEqual(const char* qname, const char* name)
    {
      const char* p = qname;
      while (*p != '\0')
        ++p;
      while (p > qname)
        if (*(--p) == ':')
        {
          ++p;
          break;
        }
      return (strcmp(p, name) == 0);
    }

  private:
    std::string m_key;
    std::vector<Element> m_attrs;
  };

  typedef SHARED_PTR<Element> ElementPtr;

  class ElementList : public std::vector<ElementPtr>
  {
  public:
    ElementList() {}
    ElementList(const std::vector<ElementPtr>& vars) : std::vector<ElementPtr>(vars) {}
    virtual ~ElementList() {}
    
    iterator FindKey(const std::string& key, iterator _begin)
    {
      for (std::vector<ElementPtr>::iterator it = _begin; it != this->end(); ++it)
        if ((*it)->GetKey() == key)
          return it;
      return this->end();
    }

    const_iterator FindKey(const std::string& key, const_iterator _begin) const
    {
      for (std::vector<ElementPtr>::const_iterator it = _begin; it != this->end(); ++it)
        if (*it && (*it)->GetKey() == key)
          return it;
      return end();
    }

    iterator FindKey(const std::string& key)
    {
      return FindKey(key, begin());
    }

    const_iterator FindKey(const std::string& key) const
    {
      return FindKey(key, begin());
    }
    
    const std::string& GetValue(const std::string& key) const
    {
      std::vector<ElementPtr>::const_iterator it = FindKey(key);
      if (it != end() && (*it))
        return (**it);
      return Element::Nil();
    }
  };

}

#endif	/* ELEMENT_H */

