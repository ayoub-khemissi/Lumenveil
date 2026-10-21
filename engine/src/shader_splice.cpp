#include "shader_splice.h"

#include <algorithm>
#include <map>
#include <set>

namespace
{
constexpr uint32_t kEndToken = 0x0000FFFFu;
constexpr uint32_t kCommentOpcode = 0xFFFEu;
constexpr uint32_t kPixelVersion3 = 0xFFFF0300u;
constexpr uint32_t kVertexVersion3 = 0xFFFE0300u;

constexpr uint32_t kOpNop = 0x00u;
constexpr uint32_t kOpMov = 0x01u;
constexpr uint32_t kOpAdd = 0x02u;
constexpr uint32_t kOpMad = 0x04u;
constexpr uint32_t kOpDp3 = 0x08u;
constexpr uint32_t kOpDp4 = 0x09u;
constexpr uint32_t kOpCall = 0x19u;
constexpr uint32_t kOpCallNz = 0x1Au;
constexpr uint32_t kOpLoop = 0x1Bu;
constexpr uint32_t kOpRet = 0x1Cu;
constexpr uint32_t kOpEndLoop = 0x1Du;
constexpr uint32_t kOpLabel = 0x1Eu;
constexpr uint32_t kOpDcl = 0x1Fu;
constexpr uint32_t kOpRep = 0x26u;
constexpr uint32_t kOpEndRep = 0x27u;
constexpr uint32_t kOpIf = 0x28u;
constexpr uint32_t kOpIfc = 0x29u;
constexpr uint32_t kOpElse = 0x2Au;
constexpr uint32_t kOpEndIf = 0x2Bu;
constexpr uint32_t kOpBreak = 0x2Cu;
constexpr uint32_t kOpBreakC = 0x2Du;
constexpr uint32_t kOpDefB = 0x2Fu;
constexpr uint32_t kOpDefI = 0x30u;
constexpr uint32_t kOpTexkill = 0x41u;
constexpr uint32_t kOpTexld = 0x42u;
constexpr uint32_t kOpDef = 0x51u;
constexpr uint32_t kOpDsx = 0x5Bu;
constexpr uint32_t kOpDsy = 0x5Cu;
constexpr uint32_t kOpTexldd = 0x5Du;
constexpr uint32_t kOpTexldl = 0x5Fu;
constexpr uint32_t kOpBreakP = 0x60u;

constexpr int kTemp = 0;
constexpr int kInput = 1;
constexpr int kConst = 2;
constexpr int kAddress = 3;
constexpr int kOutput = 6;
constexpr int kConstInt = 7;
constexpr int kColourOut = 8;
constexpr int kSampler = 10;
constexpr int kConstBool = 14;
constexpr int kLoopCounter = 15;

constexpr uint32_t kUsagePosition = 0u;
constexpr uint32_t kUsageNormal = 3u;
constexpr uint32_t kUsageTexcoord = 5u;
constexpr uint32_t kUsageColour = 10u;
constexpr uint32_t kViewPositionIndex = 3u;
constexpr uint32_t kTexcoordIndices = 16u;

constexpr uint32_t kModelFragmentUvIndex = 0u;
constexpr uint32_t kModelFragmentViewIndex = 1u;
constexpr uint32_t kModelFragmentNormalIndex = 2u;

constexpr int kPixelTemps = 32;
constexpr int kPixelInputs = 10;
constexpr int kIntConstants = 16;
constexpr uint32_t kIdentitySwizzle = 0xE4u;
constexpr uint32_t kFullMask = 0xFu;
constexpr uint32_t kXyzMask = 0x7u;
constexpr uint32_t kXyMask = 0x3u;
constexpr uint32_t kAlphaMask = 0x8u;
constexpr uint32_t kSaturateResult = 0x00100000u;
constexpr uint32_t kNegateSource = 1u;
constexpr int kOriginDepth = 4;

struct Instruction
{
    size_t at;
    size_t size;
    uint32_t opcode;
    uint32_t controls;
    int depth;
};

struct Declared
{
    uint32_t usage;
    uint32_t index;
    int reg;
    uint32_t mask;
};

struct Coordinate
{
    uint32_t usageIndex;
    int first;
    int second;
};

constexpr Coordinate kSlotCoordinates[kReliefMaxLayers + 1] = {
    {0, 0, 1}, {1, 0, 1}, {1, 3, 2}, {2, 0, 1}, {2, 3, 2},
};

int RegType(uint32_t t)
{
    return static_cast<int>(((t >> 28) & 7u) | ((t >> 8) & 0x18u));
}

int RegNumber(uint32_t t)
{
    return static_cast<int>(t & 0x7FFu);
}

bool Relative(uint32_t t)
{
    return (t & 0x2000u) != 0;
}

uint32_t WriteMask(uint32_t destination)
{
    return (destination >> 16) & kFullMask;
}

uint32_t WithRegister(uint32_t t, int type, int number)
{
    t &= ~(0x7FFu | (7u << 28) | (3u << 11));
    t |= static_cast<uint32_t>(number) & 0x7FFu;
    t |= (static_cast<uint32_t>(type) & 7u) << 28;
    t |= ((static_cast<uint32_t>(type) >> 3) & 3u) << 11;
    return t;
}

uint32_t TempDestination(int reg)
{
    return 0x80000000u | (kFullMask << 16) | static_cast<uint32_t>(reg);
}

uint32_t ColourDestination(int index)
{
    return WithRegister(0x80000000u | (kFullMask << 16), kColourOut, index);
}

uint32_t TempSource(int reg)
{
    return 0x80000000u | (kIdentitySwizzle << 16) | static_cast<uint32_t>(reg);
}

uint32_t InstructionToken(uint32_t opcode, uint32_t operands)
{
    return opcode | (operands << 24);
}

int SwizzleComponent(uint32_t t, int component)
{
    return static_cast<int>((t >> (16 + 2 * component)) & 3u);
}

uint32_t FirstTwoRepeated(uint32_t t)
{
    const uint32_t x = static_cast<uint32_t>(SwizzleComponent(t, 0));
    const uint32_t y = static_cast<uint32_t>(SwizzleComponent(t, 1));
    return (t & ~(0xFFu << 16)) | ((x | (y << 2) | (x << 4) | (y << 6)) << 16);
}

uint32_t SourceModifier(uint32_t t)
{
    return (t >> 24) & 0xFu;
}

uint32_t ComponentsRead(uint32_t source, uint32_t writeMask)
{
    uint32_t read = 0;
    for (int c = 0; c < 4; ++c)
        if (writeMask & (1u << c))
            read |= 1u << SwizzleComponent(source, c);
    return read;
}

int ReplicatedComponent(uint32_t t)
{
    const int c = SwizzleComponent(t, 0);
    for (int k = 1; k < 4; ++k)
        if (SwizzleComponent(t, k) != c)
            return -1;
    return c;
}

int FirstComponent(uint32_t mask)
{
    for (int c = 0; c < 4; ++c)
        if (mask & (1u << c))
            return c;
    return -1;
}

bool IdentityXyz(uint32_t t)
{
    return SwizzleComponent(t, 0) == 0 && SwizzleComponent(t, 1) == 1 && SwizzleComponent(t, 2) == 2;
}

void Explain(std::string* why, const std::string& text)
{
    if (why)
        *why = text;
}

bool Parse(ShaderCode code, uint32_t version, std::vector<Instruction>& out)
{
    out.clear();
    if (code.count < 2 || code.words[0] != version)
        return false;
    int depth = 0;
    size_t i = 1;
    while (i < code.count && code.words[i] != kEndToken)
    {
        const uint32_t token = code.words[i];
        const uint32_t opcode = token & 0xFFFFu;
        const size_t size = opcode == kCommentOpcode ? 1 + ((token >> 16) & 0x7FFFu) : 1 + ((token >> 24) & 0xFu);
        if (i + size > code.count)
            return false;
        if (opcode == kOpEndLoop || opcode == kOpEndRep || opcode == kOpEndIf)
            --depth;
        out.push_back({i, size, opcode, (token >> 16) & 0xFFu, depth});
        if (opcode == kOpLoop || opcode == kOpRep || opcode == kOpIf || opcode == kOpIfc)
            ++depth;
        if (depth < 0)
            return false;
        i += size;
    }
    return i < code.count && depth == 0;
}

bool IsDeclaration(uint32_t opcode)
{
    return opcode == kOpDcl || opcode == kOpDef || opcode == kOpDefI || opcode == kOpDefB ||
           opcode == kCommentOpcode;
}

bool HasDestination(uint32_t opcode)
{
    switch (opcode)
    {
    case kOpNop:
    case kOpCall:
    case kOpCallNz:
    case kOpLoop:
    case kOpRet:
    case kOpEndLoop:
    case kOpLabel:
    case kOpRep:
    case kOpEndRep:
    case kOpIf:
    case kOpIfc:
    case kOpElse:
    case kOpEndIf:
    case kOpBreak:
    case kOpBreakC:
    case kOpTexkill:
    case kOpBreakP:
        return false;
    default:
        return !IsDeclaration(opcode);
    }
}

template <typename F> void ForEachRegister(const uint32_t* words, const Instruction& in, F&& visit)
{
    if (in.opcode == kCommentOpcode)
        return;
    if (in.opcode == kOpDcl)
    {
        visit(in.at + 2);
        return;
    }
    if (in.opcode == kOpDef || in.opcode == kOpDefI || in.opcode == kOpDefB)
    {
        visit(in.at + 1);
        return;
    }
    for (size_t k = in.at + 1; k < in.at + in.size; ++k)
    {
        visit(k);
        if (Relative(words[k]))
            ++k;
    }
}

struct Scan
{
    std::vector<Instruction> instructions;
    std::vector<Declared> inputs;
    std::vector<Declared> outputs;
    std::set<int> samplers;
    std::set<int> floatConstants;
    std::set<int> intConstants;
    std::set<int> boolConstants;
    std::vector<int> literalFloats;
    std::vector<int> literalInts;
    int highestTemp = -1;
    int highestInput = -1;
    bool relativeInput = false;
    bool unexpectedType = false;
};

bool ScanShader(ShaderCode code, uint32_t version, Scan& s)
{
    if (!Parse(code, version, s.instructions))
        return false;
    const uint32_t* w = code.words;
    for (const Instruction& in : s.instructions)
    {
        if (in.opcode == kOpDcl)
        {
            const uint32_t usageToken = w[in.at + 1];
            const uint32_t reg = w[in.at + 2];
            const Declared d = {usageToken & 0x1Fu, (usageToken >> 16) & 0xFu, RegNumber(reg), WriteMask(reg)};
            if (RegType(reg) == kInput)
                s.inputs.push_back(d);
            if (RegType(reg) == kOutput)
                s.outputs.push_back(d);
            if (RegType(reg) == kSampler)
                s.samplers.insert(RegNumber(reg));
        }
        if (in.opcode == kOpDef)
            s.literalFloats.push_back(RegNumber(w[in.at + 1]));
        if (in.opcode == kOpDefI)
            s.literalInts.push_back(RegNumber(w[in.at + 1]));
        ForEachRegister(w, in, [&](size_t k) {
            const uint32_t t = w[k];
            const int type = RegType(t);
            const int number = RegNumber(t);
            switch (type)
            {
            case kTemp:
                s.highestTemp = std::max(s.highestTemp, number);
                break;
            case kInput:
                s.highestInput = std::max(s.highestInput, number);
                s.relativeInput = s.relativeInput || Relative(t);
                break;
            case kConst:
                s.floatConstants.insert(number);
                break;
            case kConstInt:
                s.intConstants.insert(number);
                break;
            case kConstBool:
                s.boolConstants.insert(number);
                break;
            case kSampler:
            case kColourOut:
            case kAddress:
            case kLoopCounter:
            case kOutput:
                break;
            default:
                s.unexpectedType = true;
                break;
            }
        });
    }
    return true;
}

const Declared* FindIn(const std::vector<Declared>& list, uint32_t usage, uint32_t index)
{
    for (const Declared& d : list)
        if (d.usage == usage && d.index == index)
            return &d;
    return nullptr;
}

const Declared* FindInput(const Scan& s, uint32_t usage, uint32_t index)
{
    return FindIn(s.inputs, usage, index);
}

const Declared* FindOutput(const Scan& s, uint32_t usage, uint32_t index)
{
    return FindIn(s.outputs, usage, index);
}

const Declared* InputAt(const Scan& s, int reg)
{
    for (const Declared& d : s.inputs)
        if (d.reg == reg)
            return &d;
    return nullptr;
}

const Declared* OutputAt(const Scan& s, int reg)
{
    for (const Declared& d : s.outputs)
        if (d.reg == reg)
            return &d;
    return nullptr;
}

bool FindViewAndNormal(const Scan& s, const Declared*& view, const Declared*& normal)
{
    view = normal = nullptr;
    for (uint32_t k = 0; k + 1 < kTexcoordIndices; ++k)
    {
        const Declared* a = FindInput(s, kUsageTexcoord, k);
        const Declared* b = FindInput(s, kUsageTexcoord, k + 1);
        if (a && b && a->mask == kXyzMask && b->mask == kXyzMask)
        {
            view = a;
            normal = b;
            return true;
        }
    }
    return false;
}

bool CoordinateMatches(const Scan& s, uint32_t coord, int slot)
{
    if (RegType(coord) != kInput || Relative(coord) || SourceModifier(coord) != 0)
        return false;
    const Declared* d = InputAt(s, RegNumber(coord));
    const Coordinate& want = kSlotCoordinates[slot];
    return d && d->usage == kUsageTexcoord && d->index == want.usageIndex &&
           SwizzleComponent(coord, 0) == want.first && SwizzleComponent(coord, 1) == want.second;
}

struct LayerFetch
{
    size_t instruction;
    int sampler;
};

bool FindLayerFetches(ShaderCode code, const Scan& s, std::vector<LayerFetch>& fetches, std::string* why)
{
    fetches.clear();
    for (size_t n = 0; n < s.instructions.size(); ++n)
    {
        const Instruction& in = s.instructions[n];
        if (in.opcode != kOpTexld || in.controls != 0 || in.depth != 0 || in.size != 4)
            continue;
        const int sampler = RegNumber(code.words[in.at + 3]);
        if (sampler <= kReliefMaxLayers)
            fetches.push_back({n, sampler});
    }
    if (fetches.empty() || fetches.size() > kReliefMaxLayers + 1)
    {
        Explain(why, "not 1 to 5 top-level fetches from samplers 0-4");
        return false;
    }
    std::vector<int> seen;
    for (const LayerFetch& f : fetches)
        seen.push_back(f.sampler);
    std::sort(seen.begin(), seen.end());
    for (size_t k = 0; k < seen.size(); ++k)
        if (seen[k] != static_cast<int>(k))
        {
            Explain(why, "the fetches do not use samplers 0..n-1 once each");
            return false;
        }
    for (const LayerFetch& f : fetches)
        if (!CoordinateMatches(s, code.words[s.instructions[f.instruction].at + 2], f.sampler))
        {
            Explain(why, "a fetch's coordinate is not the terrain layout's");
            return false;
        }
    return true;
}

bool IsFetch(uint32_t opcode)
{
    return opcode == kOpTexld || opcode == kOpTexldl || opcode == kOpTexldd;
}

bool FindDiffuseFetch(ShaderCode code, const Scan& s, size_t& fetch, std::string* why)
{
    fetch = SIZE_MAX;
    int found = 0;
    for (size_t n = 0; n < s.instructions.size(); ++n)
    {
        const Instruction& in = s.instructions[n];
        if (!IsFetch(in.opcode) || in.size < 4)
            continue;
        const uint32_t sampler = code.words[in.at + 3];
        if (RegType(sampler) == kSampler && RegNumber(sampler) == 0)
        {
            ++found;
            fetch = n;
        }
    }
    if (found != 1)
    {
        Explain(why, "not exactly one fetch from s0");
        return false;
    }
    const Instruction& in = s.instructions[fetch];
    if (in.opcode != kOpTexld || in.controls != 0 || in.depth != 0 || in.size != 4)
    {
        Explain(why, "the fetch from s0 is not a plain texld at the top level");
        return false;
    }
    return true;
}

struct SplicePlan
{
    ShaderCode client;
    ShaderCode fragment;
    const Scan* c = nullptr;
    const Scan* f = nullptr;
    std::map<int, int> inputMap;
    std::vector<uint32_t> declarations;
    std::set<size_t> offsetFetches;
    int colourInput = -1;
    bool relief = false;
    bool material = false;
};

bool SpliceChecks(const SplicePlan& p, std::string* why)
{
    const Scan& c = *p.c;
    const Scan& f = *p.f;
    if (c.unexpectedType || f.unexpectedType || f.relativeInput || !f.boolConstants.empty())
    {
        Explain(why, "a register type the splice does not handle");
        return false;
    }
    for (int reg : c.floatConstants)
        if (reg >= kReliefLiteralBase)
        {
            Explain(why, "the client shader uses constants from c180 on");
            return false;
        }
    if (!p.material)
        return true;
    for (const Instruction& in : c.instructions)
    {
        if (in.opcode == kOpCall || in.opcode == kOpCallNz || in.opcode == kOpRet || in.opcode == kOpLabel)
        {
            Explain(why, "the client shader has subroutines");
            return false;
        }
        if (IsDeclaration(in.opcode))
            continue;
        for (size_t k = in.at + 1; k < in.at + in.size; ++k)
            if (RegType(p.client.words[k]) == kColourOut && RegNumber(p.client.words[k]) > 0)
            {
                Explain(why, "the client shader writes a colour output past oC0");
                return false;
            }
    }
    return true;
}

bool EmitSplice(const SplicePlan& p, std::vector<uint32_t>& out, std::string* why)
{
    out.clear();
    if (!SpliceChecks(p, why))
        return false;
    const Scan& c = *p.c;
    const Scan& f = *p.f;
    const int tempBase = c.highestTemp + 1;
    const int offsetTemp = tempBase + f.highestTemp + 1;
    const int lightTemp = offsetTemp + 1;
    const int coordTemp = lightTemp + 1;
    const int gradientX = coordTemp + 1;
    const int gradientY = gradientX + 1;
    const int materialTemp = gradientY + 1;
    const int tagTemp = materialTemp + 1;
    if (tagTemp >= kPixelTemps)
    {
        Explain(why, "not enough temporary registers");
        return false;
    }
    std::map<int, int> literalMap;
    for (size_t k = 0; k < f.literalFloats.size(); ++k)
        literalMap[f.literalFloats[k]] = kReliefLiteralBase + static_cast<int>(k);
    if (kReliefLiteralBase + static_cast<int>(f.literalFloats.size()) > kReliefLiteralLimit)
    {
        Explain(why, "too many fragment literals");
        return false;
    }
    std::map<int, int> intMap;
    for (size_t k = 0; k < f.literalInts.size(); ++k)
    {
        const int target = kIntConstants - 1 - static_cast<int>(k);
        if (c.intConstants.count(target))
        {
            Explain(why, "integer constants collide");
            return false;
        }
        intMap[f.literalInts[k]] = target;
    }

    const uint32_t* cw = p.client.words;
    const uint32_t* fw = p.fragment.words;
    out.push_back(cw[0]);
    size_t firstBody = 0;
    while (firstBody < c.instructions.size() && IsDeclaration(c.instructions[firstBody].opcode))
    {
        const Instruction& in = c.instructions[firstBody];
        if (in.opcode != kCommentOpcode)
            out.insert(out.end(), cw + in.at, cw + in.at + in.size);
        ++firstBody;
    }
    out.insert(out.end(), p.declarations.begin(), p.declarations.end());

    const std::map<int, int>& inputMap = p.inputMap;
    auto remapFragment = [&](uint32_t t) -> uint32_t {
        const int type = RegType(t);
        const int number = RegNumber(t);
        switch (type)
        {
        case kTemp:
            return WithRegister(t, kTemp, tempBase + number);
        case kInput:
        {
            const auto mapped = inputMap.find(number);
            return WithRegister(t, kInput, mapped != inputMap.end() ? mapped->second : number);
        }
        case kConst:
            return literalMap.count(number) ? WithRegister(t, kConst, literalMap[number]) : t;
        case kConstInt:
            return intMap.count(number) ? WithRegister(t, kConstInt, intMap[number]) : t;
        case kColourOut:
        {
            const int outputs[4] = {offsetTemp, lightTemp, materialTemp, tagTemp};
            return WithRegister(t, kTemp, p.relief ? outputs[number & 3] : outputs[2 + (number & 1)]);
        }
        default:
            return t;
        }
    };

    for (const Instruction& in : f.instructions)
    {
        if (in.opcode != kOpDcl)
            continue;
        const uint32_t reg = fw[in.at + 2];
        if (RegType(reg) == kSampler && !c.samplers.count(RegNumber(reg)))
            out.insert(out.end(), fw + in.at, fw + in.at + in.size);
    }
    for (const Instruction& in : f.instructions)
    {
        if (in.opcode != kOpDef && in.opcode != kOpDefI)
            continue;
        const size_t start = out.size();
        out.insert(out.end(), fw + in.at, fw + in.at + in.size);
        out[start + 1] = remapFragment(out[start + 1]);
    }
    for (const Instruction& in : f.instructions)
    {
        if (IsDeclaration(in.opcode))
            continue;
        const int lastOutput = p.relief && p.material ? 3 : 1;
        if (in.opcode != kOpLoop && in.opcode != kOpRep)
            for (size_t k = in.at + 1; k < in.at + in.size; ++k)
                if (RegType(fw[k]) == kColourOut && RegNumber(fw[k]) > lastOutput)
                {
                    Explain(why, "the fragment writes a colour output past the ones its mode uses");
                    out.clear();
                    return false;
                }
        const size_t start = out.size();
        out.insert(out.end(), fw + in.at, fw + in.at + in.size);
        ForEachRegister(fw, in, [&](size_t k) { out[start + (k - in.at)] = remapFragment(fw[k]); });
    }

    for (size_t n = firstBody; n < c.instructions.size(); ++n)
    {
        const Instruction& in = c.instructions[n];
        if (in.opcode == kCommentOpcode)
            continue;
        if (p.relief && p.offsetFetches.count(n))
        {
            const uint32_t destination = cw[in.at + 1];
            const uint32_t coordinate = FirstTwoRepeated(cw[in.at + 2]);
            const uint32_t sampler = cw[in.at + 3];
            out.insert(out.end(), {InstructionToken(kOpDsx, 2), TempDestination(gradientX), coordinate});
            out.insert(out.end(), {InstructionToken(kOpDsy, 2), TempDestination(gradientY), coordinate});
            out.insert(out.end(),
                       {InstructionToken(kOpAdd, 3), TempDestination(coordTemp), coordinate, TempSource(offsetTemp)});
            out.insert(out.end(), {InstructionToken(kOpTexldd, 5), destination, TempSource(coordTemp), sampler,
                                   TempSource(gradientX), TempSource(gradientY)});
            continue;
        }
        const size_t start = out.size();
        out.insert(out.end(), cw + in.at, cw + in.at + in.size);
        ForEachRegister(cw, in, [&](size_t k) {
            const uint32_t t = cw[k];
            if (p.relief && RegType(t) == kInput && RegNumber(t) == p.colourInput)
                out[start + (k - in.at)] = WithRegister(t, kTemp, lightTemp);
        });
    }
    if (p.material)
    {
        out.insert(out.end(), {InstructionToken(kOpMov, 2), ColourDestination(1), TempSource(materialTemp)});
        out.insert(out.end(), {InstructionToken(kOpMov, 2), ColourDestination(2), TempSource(tagTemp)});
    }
    out.push_back(kEndToken);

    std::vector<Instruction> check;
    if (!Parse({out.data(), out.size()}, kPixelVersion3, check))
    {
        out.clear();
        Explain(why, "the spliced shader does not parse");
        return false;
    }
    return true;
}

struct Source
{
    uint32_t token;
    bool relative;
};

std::vector<Source> SourcesOf(const uint32_t* w, const Instruction& in)
{
    std::vector<Source> out;
    for (size_t k = in.at + 2; k < in.at + in.size; ++k)
    {
        out.push_back({w[k], Relative(w[k])});
        if (Relative(w[k]))
            ++k;
    }
    return out;
}

size_t LastWriter(const Scan& s, const uint32_t* w, size_t before, int type, int number, uint32_t mask)
{
    for (size_t n = std::min(before, s.instructions.size()); n-- > 0;)
    {
        const Instruction& in = s.instructions[n];
        if (!HasDestination(in.opcode) || in.size < 2)
            continue;
        const uint32_t d = w[in.at + 1];
        if (RegType(d) == type && RegNumber(d) == number && (WriteMask(d) & mask) != 0)
            return n;
    }
    return SIZE_MAX;
}

std::vector<size_t> WritersOf(const Scan& s, const uint32_t* w, int type, int number)
{
    std::vector<size_t> out;
    for (size_t n = 0; n < s.instructions.size(); ++n)
    {
        const Instruction& in = s.instructions[n];
        if (HasDestination(in.opcode) && in.size >= 2 && RegType(w[in.at + 1]) == type &&
            RegNumber(w[in.at + 1]) == number)
            out.push_back(n);
    }
    return out;
}

struct Origin
{
    int type = -1;
    int number = -1;
    uint32_t usage = 0;
};

Origin OriginOf(const Scan& s, const uint32_t* w, size_t at, const Source& src, uint32_t readMask, int depth)
{
    Origin o;
    o.type = RegType(src.token);
    o.number = RegNumber(src.token);
    if (src.relative || SourceModifier(src.token) != 0)
    {
        o.type = -1;
        return o;
    }
    if (o.type == kInput)
    {
        const Declared* d = InputAt(s, o.number);
        o.usage = d ? d->usage : 0xFFu;
        return o;
    }
    if (o.type != kTemp || depth <= 0)
        return o;
    const size_t m = LastWriter(s, w, at, kTemp, o.number, readMask);
    if (m == SIZE_MAX)
        return o;
    const Instruction& mov = s.instructions[m];
    const uint32_t d = w[mov.at + 1];
    if (mov.opcode != kOpMov || (d & kSaturateResult) || (WriteMask(d) & readMask) != readMask)
        return o;
    const std::vector<Source> from = SourcesOf(w, mov);
    return from.empty() ? o : OriginOf(s, w, m, from[0], ComponentsRead(from[0].token, readMask), depth - 1);
}

bool IsConstant(const Origin& o)
{
    return o.type == kConst;
}

bool IsVertexColour(const Origin& o)
{
    return o.type == kInput && o.usage == kUsageColour;
}

Origin OriginOfSource(const Scan& s, const uint32_t* w, size_t at, const Source& src)
{
    const uint32_t written = WriteMask(w[s.instructions[at].at + 1]) & kXyzMask;
    return OriginOf(s, w, at, src, ComponentsRead(src.token, written), kOriginDepth);
}

bool ReadsOnlyTexcoords(const Scan& s, const uint32_t* w, size_t at, int depth)
{
    for (const Source& src : SourcesOf(w, s.instructions[at]))
    {
        const int type = RegType(src.token);
        if (type == kConst && !src.relative)
            continue;
        if (type == kInput)
        {
            const Declared* d = InputAt(s, RegNumber(src.token));
            if (d && d->usage == kUsageTexcoord && !src.relative)
                continue;
            return false;
        }
        if (type != kTemp || depth <= 0)
            return false;
        const size_t m = LastWriter(s, w, at, kTemp, RegNumber(src.token), kFullMask);
        if (m == SIZE_MAX || !ReadsOnlyTexcoords(s, w, m, depth - 1))
            return false;
    }
    return true;
}

void ReadModelViewAndNormalOutputs(const Scan& s, const uint32_t* w, ModelVertexLayout& layout)
{
    const Declared* position = FindOutput(s, kUsagePosition, 0);
    if (!position)
        return;
    int viewTemp = -1;
    for (size_t n : WritersOf(s, w, kOutput, position->reg))
        if (s.instructions[n].opcode == kOpDp4)
            for (const Source& src : SourcesOf(w, s.instructions[n]))
                if (RegType(src.token) == kTemp)
                    viewTemp = RegNumber(src.token);
    if (viewTemp < 0)
        return;
    for (const Instruction& in : s.instructions)
    {
        if (in.opcode != kOpMov || in.size != 3)
            continue;
        const uint32_t d = w[in.at + 1];
        const uint32_t from = w[in.at + 2];
        const Declared* out = RegType(d) == kOutput ? OutputAt(s, RegNumber(d)) : nullptr;
        if (!out || out->usage != kUsageTexcoord || out->mask != kXyzMask || WriteMask(d) != kXyzMask ||
            RegType(from) != kTemp || RegNumber(from) != viewTemp || !IdentityXyz(from) || SourceModifier(from) != 0 ||
            Relative(from))
            continue;
        const Declared* normal = FindOutput(s, kUsageTexcoord, out->index + 1);
        if (normal && normal->mask == kXyzMask && !WritersOf(s, w, kOutput, normal->reg).empty())
        {
            layout.viewAndNormal = true;
            layout.viewIndex = static_cast<int>(out->index);
            return;
        }
    }
}

void ReadModelTexcoordOutputs(const Scan& s, const uint32_t* w, ModelVertexLayout& layout)
{
    for (const Declared& out : s.outputs)
    {
        if (out.usage != kUsageTexcoord || out.index >= 32 || (out.mask & kXyMask) != kXyMask)
            continue;
        const std::vector<size_t> writers = WritersOf(s, w, kOutput, out.reg);
        bool texcoords = !writers.empty();
        for (size_t n : writers)
            texcoords = texcoords && ReadsOnlyTexcoords(s, w, n, kOriginDepth);
        if (texcoords)
            layout.uvFromTexcoord |= 1u << out.index;
    }
}

size_t FindFacing(const Scan& s, const uint32_t* w, ModelVertexLayout& layout, int& temp, int& component)
{
    for (size_t n = 0; n < s.instructions.size(); ++n)
    {
        const Instruction& in = s.instructions[n];
        if (in.opcode != kOpDp3 || in.size != 4)
            continue;
        const uint32_t d = w[in.at + 1];
        const uint32_t light = w[in.at + 2];
        if (RegType(d) != kTemp || !(d & kSaturateResult) || RegType(light) != kConst || Relative(light) ||
            SourceModifier(light) != kNegateSource)
            continue;
        temp = RegNumber(d);
        component = FirstComponent(WriteMask(d));
        layout.lightRegister = RegNumber(light);
        return n;
    }
    return SIZE_MAX;
}

bool ReadDirectAndAmbient(const Scan& s, const uint32_t* w, size_t facing, int temp, int component,
                          ModelVertexLayout& layout)
{
    for (size_t n = facing + 1; n < s.instructions.size(); ++n)
    {
        const Instruction& in = s.instructions[n];
        if (in.opcode != kOpMad)
            continue;
        const std::vector<Source> src = SourcesOf(w, in);
        if (src.size() != 3 || RegType(src[0].token) != kTemp || RegNumber(src[0].token) != temp ||
            ReplicatedComponent(src[0].token) != component)
            continue;
        const Origin direct = OriginOfSource(s, w, n, src[1]);
        const Origin ambient = OriginOfSource(s, w, n, src[2]);
        if (!IsConstant(direct) || !IsConstant(ambient))
            return false;
        layout.directRegister = direct.number;
        layout.ambientRegister = ambient.number;
        return true;
    }
    return false;
}

void ReadLightForm(const Scan& s, const uint32_t* w, ModelVertexLayout& layout)
{
    const Declared* colour = FindOutput(s, kUsageColour, 0);
    if (!colour)
        return;
    const size_t m = LastWriter(s, w, s.instructions.size(), kOutput, colour->reg, kXyzMask);
    if (m == SIZE_MAX)
        return;
    const Instruction& last = s.instructions[m];
    const std::vector<Source> src = SourcesOf(w, last);
    if (last.opcode == kOpMad && src.size() == 3)
    {
        const Origin a = OriginOfSource(s, w, m, src[0]);
        const Origin b = OriginOfSource(s, w, m, src[1]);
        const Origin e = OriginOfSource(s, w, m, src[2]);
        if ((IsVertexColour(a) || IsVertexColour(b)) && IsConstant(e))
        {
            layout.light = ModelLightForm::Ratio;
            layout.emissiveRegister = e.number;
        }
        else if (IsConstant(a) != IsConstant(b) && IsConstant(e))
        {
            layout.light = ModelLightForm::Additive;
            layout.multiplierRegister = IsConstant(a) ? a.number : b.number;
        }
        return;
    }
    if (last.opcode != kOpAdd || src.size() != 2)
        return;
    const Origin a = OriginOfSource(s, w, m, src[0]);
    const Origin b = OriginOfSource(s, w, m, src[1]);
    if (IsConstant(a) == IsConstant(b))
        return;
    const Source& sum = IsConstant(a) ? src[1] : src[0];
    if (RegType(sum.token) != kTemp || sum.relative)
        return;
    const size_t k = LastWriter(s, w, m, kTemp, RegNumber(sum.token),
                                ComponentsRead(sum.token, WriteMask(w[last.at + 1]) & kXyzMask));
    if (k == SIZE_MAX || s.instructions[k].opcode != kOpMad)
        return;
    const std::vector<Source> terms = SourcesOf(w, s.instructions[k]);
    if (terms.size() != 3)
        return;
    const Origin x = OriginOfSource(s, w, k, terms[0]);
    const Origin y = OriginOfSource(s, w, k, terms[1]);
    const Origin z = OriginOfSource(s, w, k, terms[2]);
    if (IsVertexColour(z) && IsConstant(x) != IsConstant(y))
    {
        layout.light = ModelLightForm::Additive;
        layout.multiplierRegister = IsConstant(x) ? x.number : y.number;
    }
}

void ReadModelLight(const Scan& s, const uint32_t* w, ModelVertexLayout& layout)
{
    int temp = -1;
    int component = -1;
    const size_t facing = FindFacing(s, w, layout, temp, component);
    if (facing == SIZE_MAX || component < 0)
    {
        layout.lightRegister = -1;
        return;
    }
    if (!ReadDirectAndAmbient(s, w, facing, temp, component, layout))
        return;
    ReadLightForm(s, w, layout);
}
}

size_t ShaderWordCount(const uint32_t* code, size_t limit)
{
    if (!code || limit < 2)
        return 0;
    size_t i = 1;
    while (i < limit && code[i] != kEndToken)
    {
        const uint32_t token = code[i];
        const uint32_t opcode = token & 0xFFFFu;
        i += opcode == kCommentOpcode ? 1 + ((token >> 16) & 0x7FFFu) : 1 + ((token >> 24) & 0xFu);
    }
    return i < limit ? i + 1 : 0;
}

bool ReadTerrainPixelLayout(ShaderCode code, TerrainPixelLayout& layout, std::string* why)
{
    layout = {};
    Scan s;
    if (!ScanShader(code, kPixelVersion3, s))
    {
        Explain(why, "not ps_3_0 bytecode");
        return false;
    }
    const Declared* colour = FindInput(s, kUsageColour, 0);
    if (!colour)
    {
        Explain(why, "no COLOR0 input");
        return false;
    }
    const Declared* view = nullptr;
    const Declared* normal = nullptr;
    if ((colour->mask & kAlphaMask) || FindViewAndNormal(s, view, normal))
    {
        Explain(why, "a model shader's inputs (COLOR0 with alpha, or a view position and normal pair)");
        return false;
    }
    std::vector<LayerFetch> fetches;
    if (!FindLayerFetches(code, s, fetches, why))
        return false;
    const int n = static_cast<int>(fetches.size());
    layout.layers = n == 1 ? 1 : n - 1;
    layout.alphaSampler = n == 1 ? -1 : n - 1;
    layout.colourInput = colour->reg;
    layout.highestInput = s.highestInput;
    layout.highestTemp = s.highestTemp;
    layout.viewPositionDeclared = FindInput(s, kUsageTexcoord, kViewPositionIndex) != nullptr;
    return true;
}

bool VertexShaderWritesViewPosition(ShaderCode code)
{
    std::vector<Instruction> instructions;
    if (!Parse(code, kVertexVersion3, instructions))
        return false;
    for (const Instruction& in : instructions)
    {
        if (in.opcode != kOpDcl)
            continue;
        const uint32_t usageToken = code.words[in.at + 1];
        const uint32_t reg = code.words[in.at + 2];
        if (RegType(reg) == kOutput && (usageToken & 0x1Fu) == kUsageTexcoord &&
            ((usageToken >> 16) & 0xFu) == kViewPositionIndex && WriteMask(reg) == kXyzMask)
            return true;
    }
    return false;
}

bool SpliceTerrainShader(ShaderCode client, ShaderCode fragment, unsigned mode, std::vector<uint32_t>& out,
                         std::string* why)
{
    out.clear();
    SplicePlan p;
    p.relief = (mode & kSpliceRelief) != 0;
    p.material = (mode & kSpliceMaterial) != 0;
    if (!p.relief && !p.material)
    {
        Explain(why, "nothing to splice");
        return false;
    }
    TerrainPixelLayout layout;
    if (!ReadTerrainPixelLayout(client, layout, why))
        return false;
    Scan c;
    Scan f;
    ScanShader(client, kPixelVersion3, c);
    if (!ScanShader(fragment, kPixelVersion3, f))
    {
        Explain(why, "the terrain fragment is not ps_3_0 bytecode");
        return false;
    }
    const int viewInput = layout.viewPositionDeclared
                              ? FindInput(c, kUsageTexcoord, kViewPositionIndex)->reg
                              : layout.highestInput + 1;
    if (viewInput >= kPixelInputs)
    {
        Explain(why, "no input register left for the view position");
        return false;
    }
    for (const Declared& d : f.inputs)
    {
        if (d.usage == kUsageTexcoord && d.index == kViewPositionIndex)
        {
            p.inputMap[d.reg] = viewInput;
            continue;
        }
        const Declared* there = FindInput(c, d.usage, d.index);
        if (!there || (there->mask & d.mask) != d.mask)
        {
            Explain(why, "the client shader lacks an input the terrain fragment reads");
            return false;
        }
        p.inputMap[d.reg] = there->reg;
    }
    if (!layout.viewPositionDeclared)
        p.declarations = {InstructionToken(kOpDcl, 2), 0x80000000u | (kViewPositionIndex << 16) | kUsageTexcoord,
                          0x80000000u | (static_cast<uint32_t>(kInput) << 28) | (kXyzMask << 16) |
                              static_cast<uint32_t>(viewInput)};
    std::vector<LayerFetch> fetches;
    FindLayerFetches(client, c, fetches, why);
    for (const LayerFetch& fetch : fetches)
        if (fetch.sampler < layout.layers)
            p.offsetFetches.insert(fetch.instruction);
    p.client = client;
    p.fragment = fragment;
    p.c = &c;
    p.f = &f;
    p.colourInput = layout.colourInput;
    return EmitSplice(p, out, why);
}

bool ReadModelPixelLayout(ShaderCode code, ModelPixelLayout& layout, std::string* why)
{
    layout = {};
    Scan s;
    if (!ScanShader(code, kPixelVersion3, s))
    {
        Explain(why, "not ps_3_0 bytecode");
        return false;
    }
    if (s.unexpectedType || s.relativeInput)
    {
        Explain(why, "a register type or relative input the splice does not handle");
        return false;
    }
    const Declared* colour = FindInput(s, kUsageColour, 0);
    if (!colour || colour->mask != kFullMask)
    {
        Explain(why, "no COLOR0 input with alpha");
        return false;
    }
    if (FindInput(s, kUsageColour, 1))
    {
        Explain(why, "a COLOR1 input");
        return false;
    }
    const Declared* view = nullptr;
    const Declared* normal = nullptr;
    if (!FindViewAndNormal(s, view, normal))
    {
        Explain(why, "no view position and normal inputs");
        return false;
    }
    size_t fetch = SIZE_MAX;
    if (!FindDiffuseFetch(code, s, fetch, why))
        return false;
    const uint32_t coord = code.words[s.instructions[fetch].at + 2];
    const Declared* uv = RegType(coord) == kInput && !Relative(coord) && SourceModifier(coord) == 0
                             ? InputAt(s, RegNumber(coord))
                             : nullptr;
    if (!uv || uv->usage != kUsageTexcoord || (uv->mask & kXyMask) != kXyMask || uv == view || uv == normal ||
        SwizzleComponent(coord, 0) != 0 || SwizzleComponent(coord, 1) != 1)
    {
        Explain(why, "the fetch from s0 does not read a texture coordinate's xy");
        return false;
    }
    layout.colourInput = colour->reg;
    layout.viewInput = view->reg;
    layout.normalInput = normal->reg;
    layout.viewIndex = static_cast<int>(view->index);
    layout.diffuseInput = uv->reg;
    layout.diffuseIndex = static_cast<int>(uv->index);
    layout.highestTemp = s.highestTemp;
    return true;
}

bool UsesAddressRegister(ShaderCode code)
{
    constexpr uint32_t kMovaOpcode = 46;
    for (size_t i = 1; i < code.count && code.words[i] != kEndToken;)
    {
        const uint32_t token = code.words[i];
        const uint32_t opcode = token & 0xFFFFu;
        if (opcode == kMovaOpcode)
            return true;
        i += opcode == kCommentOpcode ? 1 + ((token >> 16) & 0x7FFFu) : 1 + ((token >> 24) & 0xFu);
    }
    return false;
}

bool ReadModelVertexLayout(ShaderCode code, ModelVertexLayout& layout)
{
    layout = {};
    Scan s;
    if (!ScanShader(code, kVertexVersion3, s))
        return false;
    const uint32_t* w = code.words;
    layout.vertexColour = FindInput(s, kUsageColour, 0) != nullptr;
    layout.normalInput = FindInput(s, kUsageNormal, 0) != nullptr;
    ReadModelViewAndNormalOutputs(s, w, layout);
    ReadModelTexcoordOutputs(s, w, layout);
    ReadModelLight(s, w, layout);
    layout.skinned = UsesAddressRegister(code);
    return layout.viewAndNormal;
}

bool SpliceModelShader(ShaderCode client, ShaderCode fragment, std::vector<uint32_t>& out, std::string* why,
                       unsigned mode)
{
    out.clear();
    if (!(mode & (kSpliceRelief | kSpliceMaterial)))
    {
        Explain(why, "nothing to splice");
        return false;
    }
    ModelPixelLayout layout;
    if (!ReadModelPixelLayout(client, layout, why))
        return false;
    Scan c;
    Scan f;
    ScanShader(client, kPixelVersion3, c);
    if (!ScanShader(fragment, kPixelVersion3, f))
    {
        Explain(why, "the model fragment is not ps_3_0 bytecode");
        return false;
    }
    SplicePlan p;
    for (const Declared& d : f.inputs)
    {
        int target = -1;
        if (d.usage == kUsageColour && d.index == 0)
            target = layout.colourInput;
        else if (d.usage == kUsageTexcoord && d.index == kModelFragmentUvIndex)
            target = layout.diffuseInput;
        else if (d.usage == kUsageTexcoord && d.index == kModelFragmentViewIndex)
            target = layout.viewInput;
        else if (d.usage == kUsageTexcoord && d.index == kModelFragmentNormalIndex)
            target = layout.normalInput;
        const Declared* there = target >= 0 ? InputAt(c, target) : nullptr;
        if (!there || (there->mask & d.mask) != d.mask)
        {
            Explain(why, "the client shader lacks an input the model fragment reads");
            return false;
        }
        p.inputMap[d.reg] = target;
    }
    p.relief = (mode & kSpliceRelief) != 0;
    p.material = (mode & kSpliceMaterial) != 0;
    if (p.relief)
    {
        size_t fetch = SIZE_MAX;
        FindDiffuseFetch(client, c, fetch, why);
        p.offsetFetches.insert(fetch);
    }
    p.client = client;
    p.fragment = fragment;
    p.c = &c;
    p.f = &f;
    p.colourInput = layout.colourInput;
    return EmitSplice(p, out, why);
}
