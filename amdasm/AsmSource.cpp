/*
 *  CLRadeonExtender - Unofficial OpenCL Radeon Extensions Library
 *  Copyright (C) 2014-2015 Mateusz Szpakowski
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <CLRX/Config.h>
#include <string>
#include <cassert>
#include <fstream>
#include <vector>
#include <stack>
#include <utility>
#include <algorithm>
#include <CLRX/utils/Utilities.h>
#include <CLRX/amdasm/Assembler.h>
#include "AsmInternals.h"

using namespace CLRX;

AsmSource::~AsmSource()
{ }

AsmFile::~AsmFile()
{ }

AsmMacroSource::~AsmMacroSource()
{ }

AsmRepeatSource::~AsmRepeatSource()
{ }

/* Asm Macro */
AsmMacro::AsmMacro(const AsmSourcePos& _pos, const Array<AsmMacroArg>& _args)
        : contentLineNo(0), pos(_pos), args(_args)
{ }

AsmMacro::AsmMacro(const AsmSourcePos& _pos, Array<AsmMacroArg>&& _args)
        : contentLineNo(0), pos(_pos), args(std::move(_args))
{ }

void AsmMacro::addLine(RefPtr<const AsmMacroSubst> macro, RefPtr<const AsmSource> source,
           const std::vector<LineTrans>& colTrans, size_t lineSize, const char* line)
{
    content.insert(content.end(), line, line+lineSize);
    if (lineSize==0 || (lineSize > 0 && line[lineSize-1] != '\n'))
        content.push_back('\n');
    colTranslations.insert(colTranslations.end(), colTrans.begin(), colTrans.end());
    if (!macro)
    {
        if (sourceTranslations.empty() || sourceTranslations.back().source != source)
            sourceTranslations.push_back({contentLineNo, source});
    }
    else
    {   // with macro
        if (sourceTranslations.empty() ||
            sourceTranslations.back().source->type != AsmSourceType::MACRO ||
            sourceTranslations.back().source.
                    staticCast<const AsmMacroSource>()->source != source ||
            sourceTranslations.back().source.
                    staticCast<const AsmMacroSource>()->macro != macro)
            sourceTranslations.push_back({contentLineNo, RefPtr<const AsmSource>(
                new AsmMacroSource{macro, source})});
    }
    contentLineNo++;
}

/* Asm Repeat */
AsmRepeat::AsmRepeat(const AsmSourcePos& _pos, uint64_t _repeatsNum)
        : contentLineNo(0), pos(_pos), repeatsNum(_repeatsNum)
{ }

void AsmRepeat::addLine(RefPtr<const AsmMacroSubst> macro, RefPtr<const AsmSource> source,
            const std::vector<LineTrans>& colTrans, size_t lineSize, const char* line)
{
    content.insert(content.end(), line, line+lineSize);
    if (lineSize==0 || (lineSize > 0 && line[lineSize-1] != '\n'))
        content.push_back('\n');
    colTranslations.insert(colTranslations.end(), colTrans.begin(), colTrans.end());
    if (sourceTranslations.empty() || sourceTranslations.back().source != source ||
        sourceTranslations.back().macro != macro)
        sourceTranslations.push_back({contentLineNo, macro, source});
    contentLineNo++;
}

AsmIRP::AsmIRP(const AsmSourcePos& _pos, const std::string& _symbolName,
               const Array<std::string>& _symValues)
        : AsmRepeat(_pos, _symValues.size()), irpc(false),
          symbolName(_symbolName), symValues(_symValues)
{ }

AsmIRP::AsmIRP(const AsmSourcePos& _pos, const std::string& _symbolName,
               Array<std::string>&& _symValues)
        : AsmRepeat(_pos, _symValues.size()), irpc(false), symbolName(_symbolName),
          symValues(std::move(_symValues))
{ }

AsmIRP::AsmIRP(const AsmSourcePos& _pos, const std::string& _symbolName,
               const std::string& _symValString)
        : AsmRepeat(_pos, std::max(_symValString.size(), size_t(1))), irpc(true),
          symbolName(_symbolName), symValues({_symValString})
{ }

/* AsmInputFilter */

AsmInputFilter::~AsmInputFilter()
{ }

LineCol AsmInputFilter::translatePos(size_t position) const
{
    auto found = std::lower_bound(colTranslations.rbegin(), colTranslations.rend(),
         LineTrans({ ssize_t(position), 0 }),
         [](const LineTrans& t1, const LineTrans& t2)
         { return t1.position > t2.position; });
    return { found->lineNo, position-found->position+1 };
}

/*
 * AsmStreamInputFilter
 */

static const size_t AsmParserLineMaxSize = 300;

AsmStreamInputFilter::AsmStreamInputFilter(const std::string& filename)
try : AsmInputFilter(AsmInputFilterType::STREAM), managed(true),
        stream(nullptr), mode(LineMode::NORMAL)
{
    source = RefPtr<const AsmSource>(new AsmFile(filename));
    stream = new std::ifstream(filename.c_str(), std::ios::binary);
    if (!*stream)
        throw Exception("Can't open include file");
    stream->exceptions(std::ios::badbit);
    buffer.reserve(AsmParserLineMaxSize);
}
catch(...)
{
    delete stream;
}

AsmStreamInputFilter::AsmStreamInputFilter(std::istream& is, const std::string& filename)
    : AsmInputFilter(AsmInputFilterType::STREAM),
      managed(false), stream(&is), mode(LineMode::NORMAL), stmtPos(0)
{
    source = RefPtr<const AsmSource>(new AsmFile(filename));
    stream->exceptions(std::ios::badbit);
    buffer.reserve(AsmParserLineMaxSize);
}

AsmStreamInputFilter::AsmStreamInputFilter(const AsmSourcePos& pos,
           const std::string& filename)
try : AsmInputFilter(AsmInputFilterType::STREAM),
      managed(true), stream(nullptr), mode(LineMode::NORMAL), stmtPos(0)
{
    if (!pos.macro)
        source = RefPtr<const AsmSource>(new AsmFile(pos.source, pos.lineNo,
                         pos.colNo, filename));
    else // if inside macro
        source = RefPtr<const AsmSource>(new AsmFile(
            RefPtr<const AsmSource>(new AsmMacroSource(pos.macro, pos.source)),
                 pos.lineNo, pos.colNo, filename));
    
    stream = new std::ifstream(filename.c_str(), std::ios::binary);
    if (!*stream)
        throw Exception("Can't open include file");
    stream->exceptions(std::ios::badbit);
    buffer.reserve(AsmParserLineMaxSize);
}
catch(...)
{
    delete stream;
}

AsmStreamInputFilter::AsmStreamInputFilter(const AsmSourcePos& pos, std::istream& is,
        const std::string& filename) : AsmInputFilter(AsmInputFilterType::STREAM),
        managed(false), stream(&is), mode(LineMode::NORMAL), stmtPos(0)
{
    if (!pos.macro)
        source = RefPtr<const AsmSource>(new AsmFile(pos.source, pos.lineNo,
                             pos.colNo, filename));
    else // if inside macro
        source = RefPtr<const AsmSource>(new AsmFile(
            RefPtr<const AsmSource>(new AsmMacroSource(pos.macro, pos.source)),
                 pos.lineNo, pos.colNo, filename));
    stream->exceptions(std::ios::badbit);
    buffer.reserve(AsmParserLineMaxSize);
}

AsmStreamInputFilter::~AsmStreamInputFilter()
{
    if (managed)
        delete stream;
}

const char* AsmStreamInputFilter::readLine(Assembler& assembler, size_t& lineSize)
{
    colTranslations.clear();
    bool endOfLine = false;
    size_t lineStart = pos;
    size_t joinStart = pos; // join Start - physical line start
    size_t destPos = pos;
    size_t backslash = false;
    bool prevAsterisk = false;
    bool asterisk = false;
    colTranslations.push_back({ssize_t(-stmtPos), lineNo});
    while (!endOfLine)
    {
        switch(mode)
        {
            case LineMode::NORMAL:
            {
                if (pos < buffer.size() && !isSpace(buffer[pos]) && buffer[pos] != ';')
                {   // putting regular string (no spaces)
                    do {
                        backslash = (buffer[pos] == '\\');
                        if (buffer[pos] == '*' &&
                            destPos > 0 && buffer[destPos-1] == '/') // longComment
                        {
                            prevAsterisk = false;
                            asterisk = false;
                            buffer[destPos-1] = ' ';
                            buffer[destPos++] = ' ';
                            mode = LineMode::LONG_COMMENT;
                            pos++;
                            break;
                        }
                        if (buffer[pos] == '#') // line comment
                        {
                            buffer[destPos++] = ' ';
                            mode = LineMode::LINE_COMMENT;
                            pos++;
                            break;
                        }
                        
                        const char old = buffer[pos];
                        buffer[destPos++] = buffer[pos++];
                        
                        if (old == '"') // if string opened
                        {
                            mode = LineMode::STRING;
                            break;
                        }
                        else if (old == '\'') // if string opened
                        {
                            mode = LineMode::LSTRING;
                            break;
                        }
                        
                    } while (pos < buffer.size() && !isSpace(buffer[pos]) &&
                                buffer[pos] != ';');
                }
                if (pos < buffer.size())
                {
                    if (buffer[pos] == '\n')
                    {
                        lineNo++;
                        endOfLine = (!backslash);
                        if (backslash) 
                        {
                            destPos--;
                            if (ssize_t(destPos-lineStart) ==
                                colTranslations.back().position)
                                colTranslations.pop_back();
                            colTranslations.push_back(
                                {ssize_t(destPos-lineStart), lineNo});
                        }
                        stmtPos = 0;
                        pos++;
                        joinStart = pos;
                        backslash = false;
                        break;
                    }
                    else if (buffer[pos] == ';' && mode == LineMode::NORMAL)
                    {   /* treat statement as separate line */
                        endOfLine = true;
                        pos++;
                        stmtPos += pos-joinStart;
                        joinStart = pos;
                        backslash = false;
                        break;
                    }
                    else if (mode == LineMode::NORMAL)
                    {   /* spaces */
                        backslash = false;
                        do {
                            buffer[destPos++] = ' ';
                            pos++;
                        } while (pos < buffer.size() && buffer[pos] != '\n' &&
                            isSpace(buffer[pos]));
                    }
                }
                break;
            }
            case LineMode::LINE_COMMENT:
            {
                while (pos < buffer.size() && buffer[pos] != '\n')
                {
                    backslash = (buffer[pos] == '\\');
                    pos++;
                    buffer[destPos++] = ' ';
                }
                if (pos < buffer.size())
                {
                    lineNo++;
                    endOfLine = (!backslash);
                    if (backslash)
                    {
                        destPos--;
                        if (ssize_t(destPos-lineStart) == colTranslations.back().position)
                            colTranslations.pop_back();
                        colTranslations.push_back({ssize_t(destPos-lineStart), lineNo});
                    }
                    else
                        mode = LineMode::NORMAL;
                    pos++;
                    joinStart = pos;
                    backslash = false;
                    stmtPos = 0;
                }
                break;
            }
            case LineMode::LONG_COMMENT:
            {
                while (pos < buffer.size() && buffer[pos] != '\n' &&
                    (!asterisk || buffer[pos] != '/'))
                {
                    backslash = (buffer[pos] == '\\');
                    prevAsterisk = asterisk;
                    asterisk = (buffer[pos] == '*');
                    pos++;
                    buffer[destPos++] = ' ';
                }
                if (pos < buffer.size())
                {
                    if ((asterisk && buffer[pos] == '/'))
                    {
                        pos++;
                        buffer[destPos++] = ' ';
                        mode = LineMode::NORMAL;
                    }
                    else // newline
                    {
                        lineNo++;
                        endOfLine = (!backslash);
                        if (backslash)
                        {
                            asterisk = prevAsterisk;
                            prevAsterisk = false;
                            destPos--;
                            if (ssize_t(destPos-lineStart) ==
                                colTranslations.back().position)
                                colTranslations.pop_back();
                            colTranslations.push_back(
                                {ssize_t(destPos-lineStart), lineNo});
                        }
                        pos++;
                        joinStart = pos;
                        backslash = false;
                        stmtPos = 0;
                    }
                }
                break;
            }
            case LineMode::STRING:
            case LineMode::LSTRING:
            {
                const char quoteChar = (mode == LineMode::STRING)?'"':'\'';
                while (pos < buffer.size() && buffer[pos] != '\n' &&
                    ((backslash&1) || buffer[pos] != quoteChar))
                {
                    if (buffer[pos] == '\\')
                        backslash++;
                    else
                        backslash = 0;
                    buffer[destPos++] = buffer[pos];
                    pos++;
                }
                if (pos < buffer.size())
                {
                    if ((backslash&1)==0 && buffer[pos] == quoteChar)
                    {
                        pos++;
                        mode = LineMode::NORMAL;
                        buffer[destPos++] = quoteChar;
                    }
                    else
                    {
                        lineNo++;
                        endOfLine = ((backslash&1)==0);
                        if (backslash&1)
                        {
                            destPos--; // ignore last backslash
                            colTranslations.push_back(
                                {ssize_t(destPos-lineStart), lineNo});
                        }
                        else
                            assembler.printWarning({lineNo, pos-joinStart+stmtPos+1},
                                        "Unterminated string: newline inserted");
                        pos++;
                        joinStart = pos;
                        stmtPos = 0;
                    }
                    backslash = false;
                }
                break;
            }
            default:
                break;
        }
        
        if (endOfLine)
            break;
        
        if (pos >= buffer.size())
        {   /* get from buffer */
            if (lineStart != 0)
            {
                std::copy_backward(buffer.begin()+lineStart, buffer.begin()+pos,
                       buffer.begin() + pos-lineStart);
                destPos -= lineStart;
                joinStart -= pos-destPos;
                pos = destPos;
                lineStart = 0;
            }
            if (pos == buffer.size())
                buffer.resize(std::max(AsmParserLineMaxSize, (pos>>1)+pos));
            
            stream->read(buffer.data()+pos, buffer.size()-pos);
            const size_t readed = stream->gcount();
            buffer.resize(pos+readed);
            if (readed == 0)
            {   // end of file. check comments
                if (mode == LineMode::LONG_COMMENT && lineStart!=pos)
                    assembler.printError({lineNo, pos-joinStart+stmtPos+1},
                           "Unterminated multi-line comment");
                if (destPos-lineStart == 0)
                {
                    lineSize = 0;
                    return nullptr;
                }
                break;
            }
        }
    }
    lineSize = destPos-lineStart;
    return buffer.data()+lineStart;
}

AsmMacroInputFilter::AsmMacroInputFilter(RefPtr<const AsmMacro> _macro,
         const AsmSourcePos& pos, const MacroArgMap& _argMap, uint64_t _macroCount)
        : AsmInputFilter(AsmInputFilterType::MACROSUBST), macro(_macro),
          argMap(_argMap), macroCount(_macroCount), contentLineNo(0), sourceTransIndex(0),
          realLinePos(0)
{
    if (macro->getSourceTransSize()!=0)
        source = macro->getSourceTrans(0).source;
    macroSubst = RefPtr<const AsmMacroSubst>(new AsmMacroSubst(pos.macro,
                   pos.source, pos.lineNo, pos.colNo));
    curColTrans = macro->getColTranslations().data();
    buffer.reserve(AsmParserLineMaxSize);
    lineNo = !macro->getColTranslations().empty() ? curColTrans[0].lineNo : 0;
    if (!macro->getColTranslations().empty())
        realLinePos = -curColTrans[0].position;
}

AsmMacroInputFilter::AsmMacroInputFilter(RefPtr<const AsmMacro> _macro,
         const AsmSourcePos& pos, MacroArgMap&& _argMap, uint64_t _macroCount)
        : AsmInputFilter(AsmInputFilterType::MACROSUBST), macro(_macro),
          argMap(std::move(_argMap)), macroCount(_macroCount),
          contentLineNo(0), sourceTransIndex(0), realLinePos(0)
{
    if (macro->getSourceTransSize()!=0)
        source = macro->getSourceTrans(0).source;
    macroSubst = RefPtr<const AsmMacroSubst>(new AsmMacroSubst(pos.macro,
                   pos.source, pos.lineNo, pos.colNo));
    curColTrans = macro->getColTranslations().data();
    buffer.reserve(AsmParserLineMaxSize);
    lineNo = !macro->getColTranslations().empty() ? curColTrans[0].lineNo : 0;
    if (!macro->getColTranslations().empty())
        realLinePos = -curColTrans[0].position;
}

const char* AsmMacroInputFilter::readLine(Assembler& assembler, size_t& lineSize)
{
    buffer.clear();
    colTranslations.clear();
    const std::vector<LineTrans>& macroColTrans = macro->getColTranslations();
    const LineTrans* colTransEnd = macroColTrans.data()+ macroColTrans.size();
    const size_t contentSize = macro->getContent().size();
    if (pos == contentSize)
    {
        lineSize = 0;
        return nullptr;
    }
    
    const char* content = macro->getContent().data();
    
    size_t nextLinePos = pos;
    while (nextLinePos < contentSize && content[nextLinePos] != '\n')
        nextLinePos++;
    
    const size_t linePos = pos;
    size_t destPos = 0;
    size_t toCopyPos = pos;
    size_t destLineStart = 0;
    // first curColTrans
    colTranslations.push_back({ ssize_t(-realLinePos), curColTrans->lineNo});
    size_t colTransThreshold = (curColTrans+1 != colTransEnd) ?
            (curColTrans[1].position>0 ? curColTrans[1].position + linePos :
                    nextLinePos) : SIZE_MAX;
    
    while (pos < contentSize && content[pos] != '\n')
    {
        if (content[pos] != '\\')
        {
            if (pos >= colTransThreshold)
            {
                curColTrans++;
                colTranslations.push_back({ssize_t(destPos + pos-toCopyPos),
                            curColTrans->lineNo});
                if (curColTrans->position >= 0)
                {
                    realLinePos = 0;
                    destLineStart = destPos + pos-toCopyPos;
                }
                colTransThreshold = (curColTrans+1 != colTransEnd) ?
                        (curColTrans[1].position>0 ? curColTrans[1].position + linePos :
                                nextLinePos) : SIZE_MAX;
            }
            pos++;
        }
        else
        {   // backslash
            if (pos >= colTransThreshold)
            {
                curColTrans++;
                colTranslations.push_back({ssize_t(destPos + pos-toCopyPos),
                            curColTrans->lineNo});
                if (curColTrans->position >= 0)
                {
                    realLinePos = 0;
                    destLineStart = destPos + pos-toCopyPos;
                }
                colTransThreshold = (curColTrans+1 != colTransEnd) ?
                        (curColTrans[1].position>0 ? curColTrans[1].position + linePos :
                                nextLinePos) : SIZE_MAX;
            }
            // copy chars to buffer
            if (pos > toCopyPos)
            {
                buffer.resize(destPos + pos-toCopyPos);
                std::copy(content + toCopyPos, content + pos, buffer.begin() + destPos);
                destPos += pos-toCopyPos;
            }
            pos++;
            bool skipColTransBetweenMacroArg = true;
            if (pos < contentSize)
            {
                if (content[pos] == '(' && pos+1 < contentSize && content[pos+1]==')')
                    pos += 2;   // skip this separator
                else
                { // extract argName
                    //ile (content[pos] >= '0'
                    const std::string symName = extractSymName(
                                content+pos, content+contentSize, false);
                    auto it = binaryMapFind(argMap.begin(), argMap.end(), symName);
                    if (it != argMap.end())
                    {   // if found
                        buffer.insert(buffer.end(), it->second.begin(), it->second.end());
                        destPos += it->second.size();
                        pos += symName.size();
                    }
                    else if (content[pos] == '@')
                    {
                        char numBuf[32];
                        const size_t numLen = itocstrCStyle(macroCount, numBuf, 32);
                        pos++;
                        buffer.insert(buffer.end(), numBuf, numBuf+numLen);
                        destPos += numLen;
                    }
                    else
                    {
                        buffer.push_back('\\');
                        destPos++;
                        skipColTransBetweenMacroArg = false;
                    }
                }
            }
            toCopyPos = pos;
            // skip colTrans between macroarg or separator
            if (skipColTransBetweenMacroArg)
            {
                while (pos > colTransThreshold)
                {
                    curColTrans++;
                    if (curColTrans->position >= 0)
                    {
                        realLinePos = 0;
                        destLineStart = destPos + pos-toCopyPos;
                    }
                    colTransThreshold = (curColTrans+1 != colTransEnd) ?
                            curColTrans[1].position : SIZE_MAX;
                }
            }
        }
    }
    if (pos > toCopyPos)
    {
        buffer.resize(destPos + pos-toCopyPos);
        std::copy(content + toCopyPos, content + pos, buffer.begin() + destPos);
        destPos += pos-toCopyPos;
    }
    lineSize = buffer.size();
    if (pos < contentSize)
    {
        if (curColTrans+1 != colTransEnd)
        {
            curColTrans++;
            if (curColTrans->position >= 0)
                realLinePos = 0;
            else
                realLinePos += lineSize - destLineStart+1;
        }
        pos++; // skip newline
    }
    lineNo = curColTrans->lineNo;
    if (sourceTransIndex+1 < macro->getSourceTransSize())
    {
        const AsmMacro::SourceTrans& fpos = macro->getSourceTrans(sourceTransIndex+1);
        if (fpos.lineNo == contentLineNo)
        {
            source = fpos.source;
            sourceTransIndex++;
        }
    }
    contentLineNo++;
    return (!buffer.empty()) ? buffer.data() : "";
}

/*
 * AsmRepeatInputFilter
 */

AsmRepeatInputFilter::AsmRepeatInputFilter(const AsmRepeat* _repeat) :
          AsmInputFilter(AsmInputFilterType::REPEAT), repeat(_repeat),
          repeatCount(0), contentLineNo(0), sourceTransIndex(0)
{
    if (_repeat->getSourceTransSize()!=0)
    {
        source = RefPtr<const AsmSource>(new AsmRepeatSource(
                    _repeat->getSourceTrans(0).source, 0, _repeat->getRepeatsNum()));
        macroSubst = _repeat->getSourceTrans(0).macro;
    }
    else
        source = RefPtr<const AsmSource>(new AsmRepeatSource(
                    RefPtr<const AsmSource>(), 0, _repeat->getRepeatsNum()));
    curColTrans = _repeat->getColTranslations().data();
    lineNo = !_repeat->getColTranslations().empty() ? curColTrans[0].lineNo : 0;
}

const char* AsmRepeatInputFilter::readLine(Assembler& assembler, size_t& lineSize)
{
    colTranslations.clear();
    const std::vector<LineTrans>& repeatColTrans = repeat->getColTranslations();
    const LineTrans* colTransEnd = repeatColTrans.data()+ repeatColTrans.size();
    const size_t contentSize = repeat->getContent().size();
    if (pos == contentSize)
    {
        repeatCount++;
        if (repeatCount == repeat->getRepeatsNum() || contentSize==0)
        {
            lineSize = 0;
            return nullptr;
        }
        sourceTransIndex = 0;
        curColTrans = repeat->getColTranslations().data();
        lineNo = curColTrans[0].lineNo;
        pos = 0;
        contentLineNo = 0;
        source = RefPtr<const AsmSource>(new AsmRepeatSource(
            repeat->getSourceTrans(0).source, repeatCount, repeat->getRepeatsNum()));
    }
    const char* content = repeat->getContent().data();
    size_t oldPos = pos;
    while (pos < contentSize && content[pos] != '\n')
        pos++;
    
    lineSize = pos - oldPos; // set new linesize
    if (pos < contentSize)
        pos++; // skip newline
    
    const LineTrans* oldCurColTrans = curColTrans;
    curColTrans++;
    while (curColTrans != colTransEnd && curColTrans->position > 0)
        curColTrans++;
    colTranslations.assign(oldCurColTrans, curColTrans);
    
    lineNo = (curColTrans != colTransEnd) ? curColTrans->lineNo : repeatColTrans[0].lineNo;
    if (sourceTransIndex+1 < repeat->getSourceTransSize())
    {
        const AsmRepeat::SourceTrans& fpos = repeat->getSourceTrans(sourceTransIndex+1);
        if (fpos.lineNo == contentLineNo)
        {
            macroSubst = fpos.macro;
            sourceTransIndex++;
            source = RefPtr<const AsmSource>(new AsmRepeatSource(
                fpos.source, repeatCount, repeat->getRepeatsNum()));
        }
    }
    contentLineNo++;
    return content + oldPos;
}

AsmIRPInputFilter::AsmIRPInputFilter(const AsmIRP* _irp) :
        AsmInputFilter(AsmInputFilterType::REPEAT), irp(_irp),
        repeatCount(0), contentLineNo(0), sourceTransIndex(0), realLinePos(0)
{
    if (_irp->getSourceTransSize()!=0)
    {
        source = RefPtr<const AsmSource>(new AsmRepeatSource(
                    _irp->getSourceTrans(0).source, 0, _irp->getRepeatsNum()));
        macroSubst = _irp->getSourceTrans(0).macro;
    }
    else
        source = RefPtr<const AsmSource>(new AsmRepeatSource(
                    RefPtr<const AsmSource>(), 0, _irp->getRepeatsNum()));
    curColTrans = _irp->getColTranslations().data();
    lineNo = !_irp->getColTranslations().empty() ? curColTrans[0].lineNo : 0;
    
    if (!_irp->getColTranslations().empty())
        realLinePos = -curColTrans[0].position;
    buffer.reserve(AsmParserLineMaxSize);
}

const char* AsmIRPInputFilter::readLine(Assembler& assembler, size_t& lineSize)
{
    buffer.clear();
    colTranslations.clear();
    const std::vector<LineTrans>& macroColTrans = irp->getColTranslations();
    const LineTrans* colTransEnd = macroColTrans.data()+ macroColTrans.size();
    const size_t contentSize = irp->getContent().size();
    if (pos == contentSize)
    {
        repeatCount++;
        if (repeatCount == irp->getRepeatsNum() || contentSize==0)
        {
            lineSize = 0;
            return nullptr;
        }
        sourceTransIndex = 0;
        curColTrans = irp->getColTranslations().data();
        lineNo = curColTrans[0].lineNo;
        realLinePos = -curColTrans[0].position;
        pos = contentLineNo = 0;
        source = RefPtr<const AsmSource>(new AsmRepeatSource(
            irp->getSourceTrans(0).source, repeatCount, irp->getRepeatsNum()));
    }
    
    const std::string& expectedSymName = irp->getSymbolName();
    const std::string& symValue = !irp->isIRPC() ? irp->getSymbolValue(repeatCount) :
            irp->getSymbolValue(0);
    const char* content = irp->getContent().data();
    
    size_t nextLinePos = pos;
    while (nextLinePos < contentSize && content[nextLinePos] != '\n')
        nextLinePos++;
    
    const size_t linePos = pos;
    size_t destPos = 0;
    size_t toCopyPos = pos;
    size_t destLineStart = 0;
    colTranslations.push_back({ ssize_t(-realLinePos), curColTrans->lineNo});
    size_t colTransThreshold = (curColTrans+1 != colTransEnd) ?
            (curColTrans[1].position>0 ? curColTrans[1].position + linePos :
                    nextLinePos) : SIZE_MAX;
    
    while (pos < contentSize && content[pos] != '\n')
    {
        if (content[pos] != '\\')
        {
            if (pos >= colTransThreshold)
            {
                curColTrans++;
                colTranslations.push_back({ssize_t(destPos + pos-toCopyPos),
                            curColTrans->lineNo});
                if (curColTrans->position >= 0)
                {
                    realLinePos = 0;
                    destLineStart = destPos + pos-toCopyPos;
                }
                colTransThreshold = (curColTrans+1 != colTransEnd) ?
                        (curColTrans[1].position>0 ? curColTrans[1].position + linePos :
                                nextLinePos) : SIZE_MAX;
            }
            pos++;
        }
        else
        {   // backslash
            if (pos >= colTransThreshold)
            {
                curColTrans++;
                colTranslations.push_back({ssize_t(destPos + pos-toCopyPos),
                            curColTrans->lineNo});
                if (curColTrans->position >= 0)
                {
                    realLinePos = 0;
                    destLineStart = destPos + pos-toCopyPos;
                }
                colTransThreshold = (curColTrans+1 != colTransEnd) ?
                        (curColTrans[1].position>0 ? curColTrans[1].position + linePos :
                                nextLinePos) : SIZE_MAX;
            }
            // copy chars to buffer
            if (pos > toCopyPos)
            {
                buffer.resize(destPos + pos-toCopyPos);
                std::copy(content + toCopyPos, content + pos, buffer.begin() + destPos);
                destPos += pos-toCopyPos;
            }
            pos++;
            bool skipColTransBetweenMacroArg = true;
            if (pos < contentSize)
            {
                if (content[pos] == '(' && pos+1 < contentSize && content[pos+1]==')')
                    pos += 2;   // skip this separator
                else
                { // extract argName
                    //ile (content[pos] >= '0'
                    const std::string symName = extractSymName(
                                content+pos, content+contentSize, false);
                    if (expectedSymName == symName)
                    {   // if found
                        if (!irp->isIRPC())
                        {
                            buffer.insert(buffer.end(), symValue.begin(), symValue.end());
                            destPos += symValue.size();
                        }
                        else if (!symValue.empty())
                        {
                            buffer.push_back(symValue[repeatCount]);
                            destPos++;
                        }
                        pos += symName.size();
                    }
                    else
                    {
                        buffer.push_back('\\');
                        destPos++;
                        skipColTransBetweenMacroArg = false;
                    }
                }
            }
            toCopyPos = pos;
            // skip colTrans between macroarg or separator
            if (skipColTransBetweenMacroArg)
            {
                while (pos > colTransThreshold)
                {
                    curColTrans++;
                    if (curColTrans->position >= 0)
                    {
                        realLinePos = 0;
                        destLineStart = destPos + pos-toCopyPos;
                    }
                    colTransThreshold = (curColTrans+1 != colTransEnd) ?
                            curColTrans[1].position : SIZE_MAX;
                }
            }
        }
    }
    if (pos > toCopyPos)
    {
        buffer.resize(destPos + pos-toCopyPos);
        std::copy(content + toCopyPos, content + pos, buffer.begin() + destPos);
        destPos += pos-toCopyPos;
    }
    lineSize = buffer.size();
    if (pos < contentSize)
    {
        if (curColTrans != colTransEnd)
        {
            curColTrans++;
            if (curColTrans != colTransEnd)
            {
                if (curColTrans->position >= 0)
                    realLinePos = 0;
                else
                    realLinePos += lineSize - destLineStart+1;
            }
        }
        pos++; // skip newline
    }
    lineNo = (curColTrans != colTransEnd) ? curColTrans->lineNo : macroColTrans[0].lineNo;
    if (sourceTransIndex+1 < irp->getSourceTransSize())
    {
        const AsmRepeat::SourceTrans& fpos = irp->getSourceTrans(sourceTransIndex+1);
        if (fpos.lineNo == contentLineNo)
        {
            macroSubst = fpos.macro;
            sourceTransIndex++;
            source = RefPtr<const AsmSource>(new AsmRepeatSource(
                fpos.source, repeatCount, irp->getRepeatsNum()));
        }
    }
    contentLineNo++;
    return (!buffer.empty()) ? buffer.data() : "";
}

/*
 * source pos
 */

static void printIndent(std::ostream& os, cxuint indentLevel)
{
    for (; indentLevel != 0; indentLevel--)
        os.write("    ", 4);
}

static RefPtr<const AsmSource> printAsmRepeats(std::ostream& os,
           RefPtr<const AsmSource> source, cxuint indentLevel)
{
    bool firstDepth = true;
    while (source->type == AsmSourceType::REPT)
    {
        auto sourceRept = source.staticCast<const AsmRepeatSource>();
        printIndent(os, indentLevel);
        os.write((firstDepth)?"In repetition ":"              ", 14);
        char numBuf[64];
        size_t size = itocstrCStyle(sourceRept->repeatCount+1, numBuf, 32);
        numBuf[size++] = '/';
        size += itocstrCStyle(sourceRept->repeatsNum, numBuf+size, 32-size);
        numBuf[size++] = ':';
        numBuf[size++] = '\n';
        os.write(numBuf, size);
        source = sourceRept->source;
        firstDepth = false;
    }
    return source;
}

void AsmSourcePos::print(std::ostream& os, cxuint indentLevel) const
{
    if (indentLevel == 10)
    {
        printIndent(os, indentLevel);
        os.write("Can't print all tree trace due to too big depth level\n", 53);
        return;
    }
    const AsmSourcePos* thisPos = this;
    bool exprFirstDepth = true;
    char numBuf[32];
    while (thisPos->exprSourcePos!=nullptr)
    {
        AsmSourcePos sourcePosToPrint = *(thisPos->exprSourcePos);
        sourcePosToPrint.exprSourcePos = nullptr;
        printIndent(os, indentLevel);
        if (sourcePosToPrint.source->type == AsmSourceType::FILE)
        {
            RefPtr<const AsmFile> file = sourcePosToPrint.source.
                        staticCast<const AsmFile>();
            if (!file->parent)
            {
                os.write((exprFirstDepth) ? "Expression evaluation from " :
                        "                      from ", 27);
                os.write(file->file.c_str(), file->file.size());
                numBuf[0] = ':';
                size_t size = 1+itocstrCStyle(sourcePosToPrint.lineNo, numBuf+1, 31);
                os.write(numBuf, size);
                if (colNo != 0)
                {
                    numBuf[0] = ':';
                    size = 1+itocstrCStyle(sourcePosToPrint.colNo, numBuf+1, 29);
                    numBuf[size++] = ':';
                    os.write(numBuf, size);
                }
                os.put('\n');
                exprFirstDepth = false;
                thisPos = thisPos->exprSourcePos;
                continue;
            }
        }
        exprFirstDepth = true;
        os.write("Expression evaluation from\n", 27);
        sourcePosToPrint.print(os, indentLevel+1);
        os.put('\n');
        thisPos = thisPos->exprSourcePos;
    }
    /* print macro tree */
    RefPtr<const AsmMacroSubst> curMacro = macro;
    RefPtr<const AsmMacroSubst> parentMacro;
    bool firstDepth = true;
    while(curMacro)
    {
        parentMacro = curMacro->parent;
        
        if (curMacro->source->type != AsmSourceType::MACRO)
        {   /* if file */
            RefPtr<const AsmFile> curFile = curMacro->source.staticCast<const AsmFile>();
            if (curMacro->source->type == AsmSourceType::REPT || curFile->parent)
            {
                if (firstDepth)
                {
                    printIndent(os, indentLevel);
                    os.write("In macro substituted from\n", 26);
                }
                AsmSourcePos nextLevelPos = { RefPtr<const AsmMacroSubst>(),
                    curMacro->source, curMacro->lineNo, curMacro->colNo };
                nextLevelPos.print(os, indentLevel+1);
                os.write((parentMacro) ? ";\n" : ":\n", 2);
                firstDepth = true;
            }
            else
            {
                printIndent(os, indentLevel);
                os.write((firstDepth) ? "In macro substituted from " :
                        "                     from ", 26);
                // leaf
                curFile = curMacro->source.staticCast<const AsmFile>();
                if (!curFile->file.empty())
                    os.write(curFile->file.c_str(), curFile->file.size());
                else // stdin
                    os.write("<stdin>", 7);
                numBuf[0] = ':';
                size_t size = 1+itocstrCStyle(curMacro->lineNo, numBuf+1, 29);
                numBuf[size++] = ':';
                os.write(numBuf, size);
                size = itocstrCStyle(curMacro->colNo, numBuf, 29);
                numBuf[size++] = (parentMacro) ? ';' : ':';
                numBuf[size++] = '\n';
                os.write(numBuf, size);
                firstDepth = false;
            }
        }
        else
        {   // if macro
            printIndent(os, indentLevel);
            os.write("In macro substituted from macro content:\n", 41);
            RefPtr<const AsmMacroSource> curMacroPos =
                    curMacro->source.staticCast<const AsmMacroSource>();
            AsmSourcePos macroPos = { curMacroPos->macro, curMacroPos->source,
                curMacro->lineNo, curMacro->colNo };
            macroPos.print(os, indentLevel+1);
            os.write((parentMacro) ? ";\n" : ":\n", 2);
            firstDepth = true;
        }
        
        curMacro = parentMacro;
    }
    /* print source tree */
    RefPtr<const AsmSource> curSource = source;
    while (curSource->type == AsmSourceType::REPT)
        curSource = curSource.staticCast<const AsmRepeatSource>()->source;
    
    if (curSource->type != AsmSourceType::MACRO)
    {   // if file
        RefPtr<const AsmFile> curFile = curSource.staticCast<const AsmFile>();
        if (curFile->parent)
        {
            RefPtr<const AsmSource> parentSource;
            bool firstDepth = true;
            while (curFile->parent)
            {
                parentSource = curFile->parent;
                
                parentSource = printAsmRepeats(os, parentSource, indentLevel);
                if (!firstDepth)
                    firstDepth = (curFile->parent != parentSource); // if repeats
                
                printIndent(os, indentLevel);
                if (parentSource->type != AsmSourceType::MACRO)
                {
                    RefPtr<const AsmFile> parentFile =
                            parentSource.staticCast<const AsmFile>();
                    os.write(firstDepth ? "In file included from " :
                            "                 from ", 22);
                    if (!parentFile->file.empty())
                        os.write(parentFile->file.c_str(), parentFile->file.size());
                    else // stdin
                        os.write("<stdin>", 7);
                    
                    numBuf[0] = ':';
                    size_t size = 1+itocstrCStyle(curFile->lineNo, numBuf+1, 29);
                    numBuf[size++] = ':';
                    os.write(numBuf, size);
                    size = itocstrCStyle(curFile->colNo, numBuf, 30);
                    curFile = parentFile.staticCast<const AsmFile>();
                    numBuf[size++] = curFile->parent ? ',' : ':';
                    numBuf[size++] = '\n';
                    os.write(numBuf, size);
                    firstDepth = false;
                }
                else
                {   /* if macro */
                    os.write("In file included from macro content:\n", 37);
                    RefPtr<const AsmMacroSource> curMacroPos =
                            parentSource.staticCast<const AsmMacroSource>();
                    AsmSourcePos macroPos = { curMacroPos->macro, curMacroPos->source,
                        curFile->lineNo, curFile->colNo };
                    macroPos.print(os, indentLevel+1);
                    os.write(":\n", 2);
                    break;
                }
            }
        }
        // leaf
        printAsmRepeats(os, source, indentLevel);
        printIndent(os, indentLevel);
        if (!curSource.staticCast<const AsmFile>()->file.empty())
            os.write(curSource.staticCast<const AsmFile>()->file.c_str(),
                     curSource.staticCast<const AsmFile>()->file.size());
        else // stdin
            os.write("<stdin>", 7);
        numBuf[0] = ':';
        size_t size = 1+itocstrCStyle(lineNo, numBuf+1, 31);
        os.write(numBuf, size);
        if (colNo != 0)
        {
            numBuf[0] = ':';
            size = 1+itocstrCStyle(colNo, numBuf+1, 31);
            os.write(numBuf, size);
        }
    }
    else
    {   // if macro
        printAsmRepeats(os, source, indentLevel);
        printIndent(os, indentLevel);
        os.write("In macro content:\n", 18);
        RefPtr<const AsmMacroSource> curMacroPos =
                curSource.staticCast<const AsmMacroSource>();
        AsmSourcePos macroPos = { curMacroPos->macro, curMacroPos->source, lineNo, colNo };
        macroPos.print(os, indentLevel+1);
    }
}