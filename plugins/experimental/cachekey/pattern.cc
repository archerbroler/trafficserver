/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/**
 * @file pattern.cc
 * @brief PRCE related classes.
 * @see pattern.h
 */

#include "pattern.h"

static void
replaceString(String &str, const String &from, const String &to)
{
  if (from.empty()) {
    return;
  }

  String::size_type start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != String::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

Pattern::Pattern() : _re(NULL), _extra(NULL), _pattern(""), _replacement(""), _tokenCount(0), _matchCount(0)
{
}

/**
 * @brief Initializes PCRE pattern by providing the subject and replacement strings.
 * @param pattern PCRE pattern, a string containing PCRE patterns, capturing groups.
 * @param replacement PCRE replacement, a string where $0 ... $9 will be replaced with the corresponding capturing groups
 * @return true if successful, false if failure
 */
bool
Pattern::init(const String &pattern, const String &replacenemt)
{
  pcreFree();

  _pattern.assign(pattern);
  _replacement.assign(replacenemt);

  _tokenCount = 0;
  _matchCount = 0;

  if (!compile()) {
    CacheKeyDebug("failed to initialize pattern:'%s', replacement:'%s'", pattern.c_str(), replacenemt.c_str());
    pcreFree();
    return false;
  }

  return true;
}

/**
 * @brief Initializes PCRE pattern by providing the pattern only or pattern+replacement in a single configuration string.
 * @see init()
 * @param config PCRE pattern <pattern> or PCRE pattern + replacement in format /<pattern>/<replacement>/
 * @return true if successful, false if failure
 */
bool
Pattern::init(const String &config)
{
  if (config[0] == '/') {
    /* This is a config in format /regex/replacement/ */
    String pattern;
    String replacement;

    size_t start = 1;
    size_t current = 0;
    size_t next = 1;
    do {
      current = next + 1;
      next = config.find_first_of("/", current);
    } while (next != String::npos && '\\' == config[next - 1]);

    if (next != String::npos) {
      pattern = config.substr(start, next - start);
    } else {
      /* Error, no closing '/' */
      CacheKeyError("failed to parse the pattern in '%s'", config.c_str());
      return false;
    }

    start = next + 1;
    do {
      current = next + 1;
      next = config.find_first_of("/", current);
    } while (next != String::npos && '\\' == config[next - 1]);

    if (next != String::npos) {
      replacement = config.substr(start, next - start);
    } else {
      /* Error, no closing '/' */
      CacheKeyError("failed to parse the replacement in '%s'", config.c_str());
      return false;
    }

    // Remove '\' which escaped '/' inside the pattern and replacement strings.
    ::replaceString(pattern, "\\/", "/");
    ::replaceString(replacement, "\\/", "/");

    return this->init(pattern, replacement);
  } else {
    return this->init(config, "");
  }

  /* Should never get here. */
  return false;
}

/**
 * @brief Checks if the pattern object was initialized with a meaningful regex pattern.
 * @return true if initialized, false if not.
 */
bool
Pattern::empty() const
{
  return _pattern.empty() || NULL == _re;
}

/**
 * @brief Frees PCRE library related resources.
 */
void
Pattern::pcreFree()
{
  if (_re) {
    pcre_free(_re);
    _re = NULL;
  }

  if (_extra) {
    pcre_free(_extra);
    _extra = NULL;
  }
}

/**
 * @bried Destructor, frees PCRE related resources.
 */
Pattern::~Pattern()
{
  pcreFree();
}

/**
 * @brief Capture or capture-and-replace depending on whether a replacement string is specified.
 * @see replace()
 * @see capture()
 * @param subject PCRE subject string
 * @param result vector of strings where the result of captures or the replacements will be returned.
 * @return true if there was a match and capture or replacement succeeded, false if failure.
 */
bool
Pattern::process(const String &subject, StringVector &result)
{
  if (!_replacement.empty()) {
    /* Replacement pattern was provided in the configuration - capture and replace. */
    String element;
    if (replace(subject, element)) {
      result.push_back(element);
    } else {
      return false;
    }
  } else {
    /* Replacement was not provided so return all capturing groups except the group zero. */
    StringVector captures;
    if (capture(subject, captures)) {
      if (captures.size() == 1) {
        result.push_back(captures[0]);
      } else {
        StringVector::iterator it = captures.begin() + 1;
        for (; it != captures.end(); it++) {
          result.push_back(*it);
        }
      }
    } else {
      return false;
    }
  }

  return true;
}

/**
 * @brief Simple failure handling routine.
 * @param PCRE subject string.
 * @return true - failed, false - no failure.
 */
bool
Pattern::failed(const String &subject) const
{
  if (_matchCount < 0) {
    switch (_matchCount) {
    case PCRE_ERROR_NOMATCH:
      CacheKeyDebug("%s does not match %s", _pattern.c_str(), subject.c_str());
      break;
    default:
      CacheKeyError("matching error %d", _matchCount);
      break;
    }

    return true;
  }

  return false;
}

/**
 * @brief PCRE matches a subject string against the the regex pattern.
 * @param subject PCRE subject
 * @return true - matched, false - did not.
 */
bool
Pattern::match(const String &subject)
{
  CacheKeyDebug("matching '%s' to '%s'", _pattern.c_str(), subject.c_str());

  if (!_re) {
    return false;
  }

  _matchCount = pcre_exec(_re, _extra, subject.c_str(), subject.length(), 0, PCRE_NOTEMPTY, NULL, 0);
  if (failed(subject)) {
    return false;
  }

  return true;
}

/**
 * @brief Return all PCRE capture groups that matched in the subject string
 * @param subject PCRE subject string
 * @param result reference to vector of strings containing all capture groups
 */
bool
Pattern::capture(const String &subject, StringVector &result)
{
  CacheKeyDebug("matching '%s' to '%s'", _pattern.c_str(), subject.c_str());

  if (!_re) {
    return false;
  }

  _matchCount = pcre_exec(_re, NULL, subject.c_str(), subject.length(), 0, PCRE_NOTEMPTY, _ovector, OVECOUNT);
  if (failed(subject)) {
    return false;
  }

  for (int i = 0; i < _matchCount; i++) {
    int start = _ovector[2 * i];
    int length = _ovector[2 * i + 1] - _ovector[2 * i];

    String dst(subject, start, length);

    CacheKeyDebug("capturing '%s' %d[%d,%d]", dst.c_str(), i, _ovector[2 * i], _ovector[2 * i + 1]);
    result.push_back(dst);
  }

  return true;
}

/**
 * @brief Replaces all replacements found in the replacement string with what matched in the PCRE capturing groups.
 * @param subject PCRE subject string
 * @param result reference to A string where the result of the replacement will be stored
 * @return true - success, false - nothing matched or failure.
 */
bool
Pattern::replace(const String &subject, String &result)
{
  CacheKeyDebug("matching '%s' to '%s'", _pattern.c_str(), subject.c_str());

  if (!_re) {
    return false;
  }

  _matchCount = pcre_exec(_re, NULL, subject.c_str(), subject.length(), 0, PCRE_NOTEMPTY, _ovector, OVECOUNT);
  if (failed(subject)) {
    return false;
  }

  /* Verify the replacement has the right number of matching groups */
  for (int i = 0; i < _tokenCount; i++) {
    if (_tokens[i] >= _matchCount) {
      CacheKeyError("invalid reference in replacement string: $%d", _tokens[i]);
      return false;
    }
  }

  int previous = 0;
  for (int i = 0; i < _tokenCount; i++) {
    int replIndex = _tokens[i];
    int start = _ovector[2 * replIndex];
    int length = _ovector[2 * replIndex + 1] - _ovector[2 * replIndex];

    String src(_replacement, _tokenOffset[i], 2);
    String dst(subject, start, length);

    CacheKeyDebug("replacing '%s' with '%s'", src.c_str(), dst.c_str());

    result.append(_replacement, previous, _tokenOffset[i] - previous);
    result.append(dst);

    previous = _tokenOffset[i] + 2; /* 2 is the size of $0 or $1 or $2, ... or $9 */
  }

  result.append(_replacement, previous, _replacement.length() - previous);

  CacheKeyDebug("replacing '%s' resulted in '%s'", _replacement.c_str(), result.c_str());

  return true;
}

/**
 * @brief PCRE compiles the regex, called only during initialization.
 * @return true if successful, false if not.
 */
bool
Pattern::compile()
{
  const char *errPtr; /* PCRE error */
  int errOffset;      /* PCRE error offset */

  CacheKeyDebug("compiling pattern:'%s', replacement:'%s'", _pattern.c_str(), _replacement.c_str());

  _re = pcre_compile(_pattern.c_str(), /* the pattern */
                     0,                /* options */
                     &errPtr,          /* for error message */
                     &errOffset,       /* for error offset */
                     NULL);            /* use default character tables */

  if (NULL == _re) {
    CacheKeyError("compile of regex '%s' at char %d: %s", _pattern.c_str(), errOffset, errPtr);

    return false;
  }

  _extra = pcre_study(_re, 0, &errPtr);

  if ((NULL == _extra) && (NULL != errPtr) && (0 != *errPtr)) {
    CacheKeyError("failed to study regex '%s': %s", _pattern.c_str(), errPtr);

    pcre_free(_re);
    _re = NULL;
    return false;
  }

  if (_replacement.empty()) {
    /* No replacement necessary - we are done. */
    return true;
  }

  _tokenCount = 0;
  bool success = true;

  for (unsigned i = 0; i < _replacement.length(); i++) {
    if (_replacement[i] == '$') {
      if (_tokenCount >= TOKENCOUNT) {
        CacheKeyError("too many tokens in replacement string: %s", _replacement.c_str());

        success = false;
        break;
      } else if (_replacement[i + 1] < '0' || _replacement[i + 1] > '9') {
        CacheKeyError("invalid replacement token $%c in %s: should be $0 - $9", _replacement[i + 1], _replacement.c_str());

        success = false;
        break;
      } else {
        /* Store the location of the replacement */
        /* Convert '0' to 0 */
        _tokens[_tokenCount] = _replacement[i + 1] - '0';
        _tokenOffset[_tokenCount] = i;
        _tokenCount++;
        /* Skip the next char */
        i++;
      }
    }
  }

  if (!success) {
    pcreFree();
  }

  return success;
}

/**
 * @brief Destructor, deletes all patterns.
 */
MultiPattern::~MultiPattern()
{
  for (std::vector<Pattern *>::iterator p = this->_list.begin(); p != this->_list.end(); ++p) {
    delete (*p);
  }
}

/**
 * @brief Check if empty.
 * @return true if the classification contains any patterns, false otherwise
 */
bool
MultiPattern::empty() const
{
  return _list.empty();
}

/**
 * @brief Adds a pattern to the multi-pattern
 *
 * The order of addition matters during the classification
 * @param pattern pattern pointer
 */
void
MultiPattern::add(Pattern *pattern)
{
  this->_list.push_back(pattern);
}

/**
 * @brief Matches the subject string against all patterns.
 * @param subject subject string.
 * @return true if any matches, false if nothing matches.
 */
bool
MultiPattern::match(const String &subject) const
{
  for (std::vector<Pattern *>::const_iterator p = this->_list.begin(); p != this->_list.end(); ++p) {
    if (NULL != (*p) && (*p)->match(subject)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Returns the name of the multi-pattern (set during the instantiation only).
 */
const String &
MultiPattern::name() const
{
  return _name;
}

/**
 * @brief Destructor, deletes all multi-patterns.
 */
Classifier::~Classifier()
{
  for (std::vector<MultiPattern *>::iterator p = _list.begin(); p != _list.end(); ++p) {
    delete (*p);
  }
}

/**
 * @brief Classifies a subject string by matching against the vector of named multi-patterns
 * in the order they were added and returns the first matching multi-pattern name.
 * @param subject string subject being classified.
 * @param name reference to a string where the name of the class that matched first will be stored.
 * @return true if something matched, false otherwise.
 */
bool
Classifier::classify(const String &subject, String &name) const
{
  bool matched = false;
  for (std::vector<MultiPattern *>::const_iterator p = _list.begin(); p != _list.end(); ++p) {
    if ((*p)->empty()) {
      continue;
    } else if ((*p)->match(subject)) {
      name = (*p)->name();
      matched = true;
      break;
    }
  }
  return matched;
}

/**
 * @brief Adds a multi-pattern to the classifier.
 *
 * The order of addition matters during the classification
 * @param pattern multi-pattern pointer
 */
void
Classifier::add(MultiPattern *pattern)
{
  _list.push_back(pattern);
}
