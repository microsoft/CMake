/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmListFileCache.h"

#include "cmListFileLexer.h"
#include "cmMessenger.h"
#include "cmOutputConverter.h"
#include "cmState.h"
#include "cmSystemTools.h"
#include "cmake.h"

#include <algorithm>
#include <assert.h>
#include <sstream>

struct cmListFileParser
{
  cmListFileParser(cmListFile* lf, cmListFileBacktrace const& lfbt,
                   cmMessenger* messenger, const char* filename);
  ~cmListFileParser();
  void IssueFileOpenError(std::string const& text) const;
  void IssueError(std::string const& text) const;
  bool ParseFile();
  bool ParseFunction(const char* name, long line);
  bool AddArgument(cmListFileLexer_Token* token,
                   cmListFileArgument::Delimiter delim);
  cmListFile* ListFile;
  cmListFileBacktrace Backtrace;
  cmMessenger* Messenger;
  const char* FileName;
  cmListFileLexer* Lexer;
  cmListFileFunction Function;
  enum
  {
    SeparationOkay,
    SeparationWarning,
    SeparationError
  } Separation;
};

cmListFileParser::cmListFileParser(cmListFile* lf,
                                   cmListFileBacktrace const& lfbt,
                                   cmMessenger* messenger,
                                   const char* filename)
  : ListFile(lf)
  , Backtrace(lfbt)
  , Messenger(messenger)
  , FileName(filename)
  , Lexer(cmListFileLexer_New())
{
}

cmListFileParser::~cmListFileParser()
{
  cmListFileLexer_Delete(this->Lexer);
}

void cmListFileParser::IssueFileOpenError(const std::string& text) const
{
  this->Messenger->IssueMessage(cmake::FATAL_ERROR, text, this->Backtrace);
}

void cmListFileParser::IssueError(const std::string& text) const
{
  cmListFileContext lfc;
  lfc.FilePath = this->FileName;
  lfc.Line = cmListFileLexer_GetCurrentLine(this->Lexer);
  cmListFileBacktrace lfbt = this->Backtrace;
  lfbt = lfbt.Push(lfc);
  this->Messenger->IssueMessage(cmake::FATAL_ERROR, text, lfbt);
  cmSystemTools::SetFatalErrorOccured();
}

bool cmListFileParser::ParseFile()
{
  // Open the file.
  cmListFileLexer_BOM bom;
  if (!cmListFileLexer_SetFileName(this->Lexer, this->FileName, &bom)) {
    this->IssueFileOpenError("cmListFileCache: error can not open file.");
    return false;
  }

  if (bom == cmListFileLexer_BOM_Broken) {
    cmListFileLexer_SetFileName(this->Lexer, nullptr, nullptr);
    this->IssueFileOpenError("Error while reading Byte-Order-Mark. "
                             "File not seekable?");
    return false;
  }

  // Verify the Byte-Order-Mark, if any.
  if (bom != cmListFileLexer_BOM_None && bom != cmListFileLexer_BOM_UTF8) {
    cmListFileLexer_SetFileName(this->Lexer, nullptr, nullptr);
    this->IssueFileOpenError(
      "File starts with a Byte-Order-Mark that is not UTF-8.");
    return false;
  }

  // Use a simple recursive-descent parser to process the token
  // stream.
  bool haveNewline = true;
  while (cmListFileLexer_Token* token = cmListFileLexer_Scan(this->Lexer)) {
    if (token->type == cmListFileLexer_Token_Space) {
    } else if (token->type == cmListFileLexer_Token_Newline) {
      haveNewline = true;
    } else if (token->type == cmListFileLexer_Token_CommentBracket) {
      haveNewline = false;
    } else if (token->type == cmListFileLexer_Token_Identifier) {
      if (haveNewline) {
        haveNewline = false;
        if (this->ParseFunction(token->text, token->line)) {
          this->ListFile->Functions.push_back(this->Function);
        } else {
          return false;
        }
      } else {
        std::ostringstream error;
        error << "Parse error.  Expected a newline, got "
              << cmListFileLexer_GetTypeAsString(this->Lexer, token->type)
              << " with text \"" << token->text << "\".";
        this->IssueError(error.str());
        return false;
      }
    } else {
      std::ostringstream error;
      error << "Parse error.  Expected a command name, got "
            << cmListFileLexer_GetTypeAsString(this->Lexer, token->type)
            << " with text \"" << token->text << "\".";
      this->IssueError(error.str());
      return false;
    }
  }
  return true;
}

bool cmListFile::ParseFile(const char* filename, cmMessenger* messenger,
                           cmListFileBacktrace const& lfbt)
{
  if (!cmSystemTools::FileExists(filename) ||
      cmSystemTools::FileIsDirectory(filename)) {
    return false;
  }

  bool parseError = false;

  {
    cmListFileParser parser(this, lfbt, messenger, filename);
    parseError = !parser.ParseFile();
  }

  return !parseError;
}

bool cmListFileParser::ParseFunction(const char* name, long line)
{
  // Ininitialize a new function call.
  this->Function = cmListFileFunction();
  this->Function.Name = name;
  this->Function.Line = line;

  // Command name has already been parsed.  Read the left paren.
  cmListFileLexer_Token* token;
  while ((token = cmListFileLexer_Scan(this->Lexer)) &&
         token->type == cmListFileLexer_Token_Space) {
  }
  if (!token) {
    std::ostringstream error;
    /* clang-format off */
    error << "Unexpected end of file.\n"
          << "Parse error.  Function missing opening \"(\".";
    /* clang-format on */
    this->IssueError(error.str());
    return false;
  }
  if (token->type != cmListFileLexer_Token_ParenLeft) {
    std::ostringstream error;
    error << "Parse error.  Expected \"(\", got "
          << cmListFileLexer_GetTypeAsString(this->Lexer, token->type)
          << " with text \"" << token->text << "\".";
    this->IssueError(error.str());
    return false;
  }

  // Arguments.
  unsigned long lastLine;
  unsigned long parenDepth = 0;
  this->Separation = SeparationOkay;
  while ((lastLine = cmListFileLexer_GetCurrentLine(this->Lexer),
          token = cmListFileLexer_Scan(this->Lexer))) {
    if (token->type == cmListFileLexer_Token_Space ||
        token->type == cmListFileLexer_Token_Newline) {
      this->Separation = SeparationOkay;
      continue;
    }
    if (token->type == cmListFileLexer_Token_ParenLeft) {
      parenDepth++;
      this->Separation = SeparationOkay;
      if (!this->AddArgument(token, cmListFileArgument::Unquoted)) {
        return false;
      }
    } else if (token->type == cmListFileLexer_Token_ParenRight) {
      if (parenDepth == 0) {
        return true;
      }
      parenDepth--;
      this->Separation = SeparationOkay;
      if (!this->AddArgument(token, cmListFileArgument::Unquoted)) {
        return false;
      }
      this->Separation = SeparationWarning;
    } else if (token->type == cmListFileLexer_Token_Identifier ||
               token->type == cmListFileLexer_Token_ArgumentUnquoted) {
      if (!this->AddArgument(token, cmListFileArgument::Unquoted)) {
        return false;
      }
      this->Separation = SeparationWarning;
    } else if (token->type == cmListFileLexer_Token_ArgumentQuoted) {
      if (!this->AddArgument(token, cmListFileArgument::Quoted)) {
        return false;
      }
      this->Separation = SeparationWarning;
    } else if (token->type == cmListFileLexer_Token_ArgumentBracket) {
      if (!this->AddArgument(token, cmListFileArgument::Bracket)) {
        return false;
      }
      this->Separation = SeparationError;
    } else if (token->type == cmListFileLexer_Token_CommentBracket) {
      this->Separation = SeparationError;
    } else {
      // Error.
      std::ostringstream error;
      error << "Parse error.  Function missing ending \")\".  "
            << "Instead found "
            << cmListFileLexer_GetTypeAsString(this->Lexer, token->type)
            << " with text \"" << token->text << "\".";
      this->IssueError(error.str());
      return false;
    }
  }

  std::ostringstream error;
  cmListFileContext lfc;
  lfc.FilePath = this->FileName;
  lfc.Line = lastLine;
  cmListFileBacktrace lfbt = this->Backtrace;
  lfbt = lfbt.Push(lfc);
  error << "Parse error.  Function missing ending \")\".  "
        << "End of file reached.";
  this->Messenger->IssueMessage(cmake::FATAL_ERROR, error.str(), lfbt);
  return false;
}

bool cmListFileParser::AddArgument(cmListFileLexer_Token* token,
                                   cmListFileArgument::Delimiter delim)
{
  cmListFileArgument a(token->text, delim, token->line);
  this->Function.Arguments.push_back(a);
  if (this->Separation == SeparationOkay) {
    return true;
  }
  bool isError = (this->Separation == SeparationError ||
                  delim == cmListFileArgument::Bracket);
  std::ostringstream m;
  cmListFileContext lfc;
  lfc.FilePath = this->FileName;
  lfc.Line = token->line;
  cmListFileBacktrace lfbt = this->Backtrace;
  lfbt = lfbt.Push(lfc);

  m << "Syntax " << (isError ? "Error" : "Warning") << " in cmake code at "
    << "column " << token->column << "\n"
    << "Argument not separated from preceding token by whitespace.";
  /* clang-format on */
  if (isError) {
    this->Messenger->IssueMessage(cmake::FATAL_ERROR, m.str(), lfbt);
    return false;
  }
  this->Messenger->IssueMessage(cmake::AUTHOR_WARNING, m.str(), lfbt);
  return true;
}

std::map<size_t, cmListFileContext> s_idToFrameMap;
std::map<cmListFileContext, size_t> s_frameToIdMap;
std::map<size_t, cmStateSnapshot> s_idToSnapshotMap;
std::map<cmStateSnapshot, size_t> s_SnapshotToIdMap;

template<typename D> size_t ComputeId(D const & data, 
                                      std::map<size_t, D> & idToDataMap, 
                                      std::map<D, size_t> & dataToIdMap)
{
  auto it = dataToIdMap.find(data);
  if (it == dataToIdMap.end()) {
    // Zero is a special id indicating not found. Always start at 1
    it = dataToIdMap.emplace(data, dataToIdMap.size()+1).first;
    idToDataMap.emplace(it->second, it->first);
  }
  return it->second;
}

size_t ComputeSnapshotId(cmStateSnapshot const& snapshot)
{
  return ComputeId(snapshot, s_idToSnapshotMap, s_SnapshotToIdMap);
}

size_t ComputeFrameId(cmListFileContext const& frame)
{
  return ComputeId(frame, s_idToFrameMap, s_frameToIdMap);
}

cmListFileBacktrace::cmListFileBacktrace()
  : SnapshotId(0)
{
}

cmListFileBacktrace::cmListFileBacktrace(cmStateSnapshot const& snapshot)
  : SnapshotId(ComputeSnapshotId(snapshot))
{
}

cmListFileBacktrace::cmListFileBacktrace(cmListFileBacktrace const& r)
  : SnapshotId(r.SnapshotId)
  , Entries(r.Entries)
{
}

cmListFileBacktrace& cmListFileBacktrace::operator=(
  cmListFileBacktrace const& r)
{
  this->SnapshotId = r.SnapshotId;
  this->Entries = r.Entries;
  return *this;
}

cmListFileBacktrace::~cmListFileBacktrace()
{
}

cmListFileBacktrace cmListFileBacktrace::Push(std::string const& file) const
{
  // We are entering a file-level scope but have not yet reached
  // any specific line or command invocation within it.  This context
  // is useful to print when it is at the top but otherwise can be
  // skipped during call stack printing.
  cmListFileContext lfc;
  lfc.FilePath = file;
  return Push(lfc);
}

cmListFileBacktrace cmListFileBacktrace::Push(
  cmListFileContext const& lfc) const
{
  cmListFileBacktrace copy(*this);
  copy.Entries.push_front(ComputeFrameId(lfc));
  return copy;
}

cmListFileBacktrace cmListFileBacktrace::Pop() const
{
  assert(this->Entries.size());
  cmListFileBacktrace copy(*this);
  copy.Entries.pop_front();
  return copy;
}

cmListFileContext const& cmListFileBacktrace::Top() const
{
  if (this->Entries.size()) {
    return s_idToFrameMap.at(*this->Entries.begin());
  }
  static cmListFileContext const empty;
  return empty;
}

cmStateSnapshot const& cmListFileBacktrace::GetBottom() const
{
  if (this->SnapshotId != 0) {
    return s_idToSnapshotMap.at(this->SnapshotId);
  }

  static cmStateSnapshot const empty;
  return empty;
}


void cmListFileBacktrace::PrintTitle(std::ostream& out) const
{
  if (!this->Entries.size()) {
    return;
  }
  auto & bottom = this->GetBottom();
  cmOutputConverter converter(bottom);
  cmListFileContext lfc = Top();
  if (!bottom.GetState()->GetIsInTryCompile()) {
    lfc.FilePath = converter.ConvertToRelativePath(
      bottom.GetState()->GetSourceDirectory(), lfc.FilePath);
  }
  out << (lfc.Line ? " at " : " in ") << lfc;
}

void cmListFileBacktrace::PrintCallStack(std::ostream& out) const
{
  if (this->Entries.size() <= 1) {
    return;
  }

  auto & bottom = this->GetBottom();
  bool first = true;
  cmOutputConverter converter(bottom);
  auto it = Entries.begin();
  for (it++; it != Entries.end(); it++) {
    auto & entry = s_idToFrameMap.at(*it);
    if (entry.Name.empty()) {
      // Skip this whole-file scope.  When we get here we already will
      // have printed a more-specific context within the file.
      continue;
    }
    if (first) {
      first = false;
      out << "Call Stack (most recent call first):\n";
    }
    cmListFileContext lfc = entry;
    if (!bottom.GetState()->GetIsInTryCompile()) {
      lfc.FilePath = converter.ConvertToRelativePath(
        bottom.GetState()->GetSourceDirectory(), lfc.FilePath);
    }
    out << "  " << lfc << "\n";
  }
}

size_t cmListFileBacktrace::Depth() const
{
  // TODO: This seems to have returned one minus the number of entries before.
  return this->Entries.size();
}

std::ostream& operator<<(std::ostream& os, cmListFileContext const& lfc)
{
  os << lfc.FilePath;
  if (lfc.Line) {
    os << ":" << lfc.Line;
    if (!lfc.Name.empty()) {
      os << " (" << lfc.Name << ")";
    }
  }
  return os;
}

bool operator<(const cmListFileContext& lhs, const cmListFileContext& rhs)
{
  if (lhs.Line != rhs.Line) {
    return lhs.Line < rhs.Line;
  }
  return lhs.FilePath < rhs.FilePath;
}

bool operator==(const cmListFileContext& lhs, const cmListFileContext& rhs)
{
  return lhs.Line == rhs.Line && lhs.FilePath == rhs.FilePath;
}

bool operator!=(const cmListFileContext& lhs, const cmListFileContext& rhs)
{
  return !(lhs == rhs);
}
