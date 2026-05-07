#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>

#include "backend_codegen.h"

using namespace ir;
using namespace regalloc;

// ================== Assembly-level Peephole Optimizer ==================
namespace {

int gPcrelLabelCounter = 0;

inline bool startsWith(const std::string& s, const std::string& prefix) {
  return s.rfind(prefix, 0) == 0;
}

inline std::string trim(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) b++;
  size_t e = s.size();
  while (e > b && (s[e-1] == ' ' || s[e-1] == '\t')) e--;
  return s.substr(b, e - b);
}

inline bool isLabelLine(const std::string& line) {
  std::string t = trim(line);
  return !t.empty() && t.back() == ':';
}

inline bool isFloatAsmLine(const std::string& line) {
  std::string t = trim(line);
  return startsWith(t, "fadd.") || startsWith(t, "fsub.") ||
         startsWith(t, "fmul.") || startsWith(t, "fdiv.") ||
         startsWith(t, "fneg.") || startsWith(t, "feq.") ||
         startsWith(t, "flt.") || startsWith(t, "fle.") ||
         startsWith(t, "fcvt.") || startsWith(t, "fmv.") ||
         startsWith(t, "flw ") || startsWith(t, "fsw ");
}

struct ArgLocation {
  bool useFloatReg = false;
  bool useIntReg = false;
  int regIndex = -1;
  int stackIndex = -1;
};

inline ArgLocation classifyArgLocation(ValueType type, int& intRegCount,
                                       int& floatRegCount, int& stackCount) {
  ArgLocation loc;
  if (type == ValueType::F32 && floatRegCount < 8) {
    loc.useFloatReg = true;
    loc.regIndex = floatRegCount++;
    return loc;
  }
  if (intRegCount < 8) {
    loc.useIntReg = true;
    loc.regIndex = intRegCount++;
    return loc;
  }
  loc.stackIndex = stackCount++;
  return loc;
}

// Optimize move chains: mv t0, a0; mv t1, t0 -> mv t1, a0
std::vector<std::string> optimizeMoveChains(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  result.reserve(lines.size());
  
  std::string prevDest, prevSrc;
  
  for (const auto& line : lines) {
    std::string t = trim(line);
    
    // Check for "mv x, x" (self-move) - remove it
    if (startsWith(t, "addi ")) {
      // addi rd, rs, 0 is a move
      size_t comma1 = t.find(',');
      if (comma1 != std::string::npos) {
        std::string rest = t.substr(comma1 + 1);
        size_t comma2 = rest.find(',');
        if (comma2 != std::string::npos) {
          std::string rd = trim(t.substr(5, comma1 - 5));
          std::string rs = trim(rest.substr(0, comma2));
          std::string imm = trim(rest.substr(comma2 + 1));
          
          if (imm == "0" && rd == rs) {
            // Self-move, skip
            continue;
          }
          
          // Check for move chain optimization
          if (imm == "0" && !prevDest.empty() && rs == prevDest) {
            // Current: addi rd, prevDest, 0
            // Replace with: addi rd, prevSrc, 0
            result.push_back("\taddi " + rd + ", " + prevSrc + ", 0");
            prevDest = rd;
            // prevSrc stays the same
            continue;
          }
          
          if (imm == "0") {
            prevDest = rd;
            prevSrc = rs;
          } else {
            prevDest.clear();
            prevSrc.clear();
          }
        }
      }
    } else if (isLabelLine(line) || startsWith(t, "j ") || startsWith(t, "jal ") ||
               startsWith(t, "bne ") || startsWith(t, "beq ") || 
               startsWith(t, "call ") || startsWith(t, "ret")) {
      // Control flow breaks the chain
      prevDest.clear();
      prevSrc.clear();
    } else {
      // Other instructions may define registers, conservatively clear
      prevDest.clear();
      prevSrc.clear();
    }
    
    result.push_back(line);
  }
  
  return result;
}

// Remove store followed by load of same location to same register
std::vector<std::string> optimizeLoadStore(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  result.reserve(lines.size());

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string t = trim(lines[i]);
    if (isFloatAsmLine(t)) {
      result.push_back(lines[i]);
      continue;
    }

    // Pattern: sw rX, offset(base) followed by lw rX, offset(base)
    if (startsWith(t, "sw ") && i + 1 < lines.size()) {
      std::string next = trim(lines[i + 1]);
      if (isFloatAsmLine(next)) {
        result.push_back(lines[i]);
        continue;
      }
      if (startsWith(next, "lw ")) {
        // Extract register and offset from both
        std::string swRest = t.substr(3);
        std::string lwRest = next.substr(3);

        size_t swComma = swRest.find(',');
        size_t lwComma = lwRest.find(',');

        if (swComma != std::string::npos && lwComma != std::string::npos) {
          std::string swReg = trim(swRest.substr(0, swComma));
          std::string lwReg = trim(lwRest.substr(0, lwComma));
          std::string swOffset = trim(swRest.substr(swComma + 1));
          std::string lwOffset = trim(lwRest.substr(lwComma + 1));

          if (swReg == lwReg && swOffset == lwOffset) {
            // Skip the redundant lw
            result.push_back(lines[i]);
            i++;  // Skip next line (lw)
            continue;
          }
        }
      }
    }

    result.push_back(lines[i]);
  }

  return result;
}

// Remove redundant operations like add with zero
std::vector<std::string> optimizeRedundantOps(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  result.reserve(lines.size());

  // Track known values for simple constant propagation
  std::unordered_map<std::string, int> knownValues;

  for (const auto& line : lines) {
    std::string t = trim(line);
    bool skip = false;

    if (isFloatAsmLine(t)) {
      knownValues.clear();
      result.push_back(line);
      continue;
    }

    // Track li instructions
    if (startsWith(t, "addi ") && t.find("x0") != std::string::npos) {
      // addi rd, x0, imm is li rd, imm
      size_t comma1 = t.find(',');
      if (comma1 != std::string::npos) {
        std::string rest = t.substr(comma1 + 1);
        size_t comma2 = rest.find(',');
        if (comma2 != std::string::npos) {
          std::string rs = trim(rest.substr(0, comma2));
          if (rs == "x0") {
            std::string rd = trim(t.substr(5, comma1 - 5));
            std::string immStr = trim(rest.substr(comma2 + 1));
            try {
              int imm = std::stoi(immStr);
              knownValues[rd] = imm;
            } catch (...) {}
          }
        }
      }
    }

    // Check for add with known zero
    if (startsWith(t, "add ")) {
      size_t comma1 = t.find(',');
      if (comma1 != std::string::npos) {
        std::string rest = t.substr(comma1 + 1);
        size_t comma2 = rest.find(',');
        if (comma2 != std::string::npos) {
          std::string dest = trim(t.substr(4, comma1 - 4));
          std::string src1 = trim(rest.substr(0, comma2));
          std::string src2 = trim(rest.substr(comma2 + 1));

          // add dest, src1, src2 where src2 is known 0 and dest == src1
          if (knownValues.count(src2) && knownValues[src2] == 0 && dest == src1) {
            skip = true;
          }
        }
      }
    }

    // Check for mul with known 1
    if (startsWith(t, "mul ")) {
      size_t comma1 = t.find(',');
      if (comma1 != std::string::npos) {
        std::string rest = t.substr(comma1 + 1);
        size_t comma2 = rest.find(',');
        if (comma2 != std::string::npos) {
          std::string dest = trim(t.substr(4, comma1 - 4));
          std::string src1 = trim(rest.substr(0, comma2));
          std::string src2 = trim(rest.substr(comma2 + 1));

          if (knownValues.count(src2) && knownValues[src2] == 1 && dest == src1) {
            skip = true;
          }
        }
      }
    }

    // Clear known values on control flow, memory traffic, or redefinition.
    if (isLabelLine(line) || startsWith(t, "j ") || startsWith(t, "jal ") ||
        startsWith(t, "call ") || startsWith(t, "ret") ||
        startsWith(t, "bne ") || startsWith(t, "beq ") ||
        startsWith(t, "lw ") || startsWith(t, "sw ")) {
      knownValues.clear();
    }

    if (!skip) {
      result.push_back(line);
    }
  }

  return result;
}

// Remove jumps to next label
std::vector<std::string> optimizeJumpToNext(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  result.reserve(lines.size());
  
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string t = trim(lines[i]);
    
    // Check for "jal x0, label" or "j label"
    std::string target;
    if (startsWith(t, "jal x0, ")) {
      target = trim(t.substr(8));
    } else if (startsWith(t, "j ")) {
      target = trim(t.substr(2));
    }
    
    if (!target.empty()) {
      // Find next non-empty line
      size_t j = i + 1;
      while (j < lines.size() && trim(lines[j]).empty()) j++;
      
      if (j < lines.size()) {
        std::string nextT = trim(lines[j]);
        // Check if it's the target label
        if (nextT == target + ":") {
          // Skip this jump
          continue;
        }
      }
    }
    
    result.push_back(lines[i]);
  }
  
  return result;
}

// Check if a word (register name) appears in a line
inline bool containsWord(const std::string& line, const std::string& reg) {
  for (size_t i = 0; i + reg.size() <= line.size(); ++i) {
    if (line.compare(i, reg.size(), reg) != 0) continue;
    char l = (i == 0) ? ' ' : line[i - 1];
    char r = (i + reg.size() >= line.size()) ? ' ' : line[i + reg.size()];
    auto okb = [](char c) { return !(isalnum((unsigned char)c) || c == '_'); };
    if (okb(l) && okb(r)) return true;
  }
  return false;
}

// ================== Small Constant Loop Unrolling ==================
// Unroll small loops (bound <= 16) directly in assembly
// Pattern:
//   Lx:
//     ... (loop body)
//     slti cond, iv, N
//     beqz cond, exit
//     ... (body continuation)
//     addi tmp, iv, 1
//     mv iv, tmp
//     j Lx
//   exit:
std::vector<std::string> optimizeSmallConstLoops(const std::vector<std::string>& lines) {
  std::vector<std::string> result = lines;
  bool anyChange = false;
  
  for (size_t i = 0; i < result.size(); ++i) {
    std::string header;
    if (!isLabelLine(result[i])) continue;
    header = trim(result[i]);
    header = header.substr(0, header.size() - 1);  // Remove ':'
    
    // Also handle function internal labels (loop_while_cond*, etc.)
    if (header.empty()) continue;
    bool isLoopHeader = (header[0] == 'L') || 
                        (header.find("_while_cond") != std::string::npos) ||
                        (header.find("_for_cond") != std::string::npos);
    if (!isLoopHeader) continue;
    
    // Look for loop bound pattern: either slti or addi+slt
    size_t sltiIdx = SIZE_MAX;
    size_t beqzIdx = SIZE_MAX;
    std::string condReg, sltiSrcReg, exitLabel;
    int bound = -1;
    
    for (size_t k = i + 1; k < result.size() && k <= i + 15; ++k) {
      std::string t = trim(result[k]);
      
      // Pattern 1: slti rd, rs, imm
      if (startsWith(t, "slti ")) {
        std::string rest = trim(t.substr(5));
        size_t c1 = rest.find(',');
        if (c1 == std::string::npos) continue;
        std::string rd = trim(rest.substr(0, c1));
        rest = trim(rest.substr(c1 + 1));
        size_t c2 = rest.find(',');
        if (c2 == std::string::npos) continue;
        std::string rs = trim(rest.substr(0, c2));
        std::string immS = trim(rest.substr(c2 + 1));
        try { bound = std::stoi(immS); } catch (...) { continue; }
        if (bound <= 0 || bound > 16) continue;
        sltiIdx = k;
        condReg = rd;
        sltiSrcReg = rs;
        
        // Find beqz cond, exit
        for (size_t m = k + 1; m < result.size() && m <= k + 3; ++m) {
          std::string u = trim(result[m]);
          if (u.empty()) continue;
          if (!startsWith(u, "beqz ")) break;
          std::string brrest = trim(u.substr(5));
          size_t cb = brrest.find(',');
          if (cb == std::string::npos) break;
          std::string r = trim(brrest.substr(0, cb));
          std::string lab = trim(brrest.substr(cb + 1));
          if (r != condReg) break;
          beqzIdx = m;
          exitLabel = lab;
          break;
        }
        break;
      }
      
      // Pattern 2: addi tX, x0, imm followed by slt rd, rs, tX and bne rd, x0, body
      // Our backend generates: slt t0, t1, t5; bne t0, x0, body; jal x0, exit
      if (startsWith(t, "slt ") && !startsWith(t, "slti ") && !startsWith(t, "sltu ")) {
        // Parse slt rd, rs1, rs2
        std::string rest = trim(t.substr(4));
        size_t c1 = rest.find(',');
        if (c1 == std::string::npos) continue;
        std::string rd = trim(rest.substr(0, c1));
        rest = trim(rest.substr(c1 + 1));
        size_t c2 = rest.find(',');
        if (c2 == std::string::npos) continue;
        std::string rs1 = trim(rest.substr(0, c2));
        std::string rs2 = trim(rest.substr(c2 + 1));
        
        // Look back for addi rs2, x0, imm
        for (size_t pk = k; pk > i && pk > k - 5; --pk) {
          std::string pt = trim(result[pk - 1]);
          if (startsWith(pt, "addi ")) {
            std::string prest = trim(pt.substr(5));
            size_t pc1 = prest.find(',');
            if (pc1 == std::string::npos) continue;
            std::string prd = trim(prest.substr(0, pc1));
            prest = trim(prest.substr(pc1 + 1));
            size_t pc2 = prest.find(',');
            if (pc2 == std::string::npos) continue;
            std::string prs = trim(prest.substr(0, pc2));
            std::string pimm = trim(prest.substr(pc2 + 1));
            
            if (prd == rs2 && prs == "x0") {
              try { bound = std::stoi(pimm); } catch (...) { continue; }
              if (bound <= 0 || bound > 16) { bound = -1; continue; }
              
              sltiIdx = k;
              condReg = rd;
              sltiSrcReg = rs1;
              
              // Find bne cond, x0, body followed by jal x0, exit
              for (size_t m = k + 1; m < result.size() && m <= k + 3; ++m) {
                std::string u = trim(result[m]);
                if (u.empty()) continue;
                if (startsWith(u, "bne ")) {
                  // bne rd, x0, body
                  std::string brrest = trim(u.substr(4));
                  size_t cb1 = brrest.find(',');
                  if (cb1 == std::string::npos) continue;
                  std::string r = trim(brrest.substr(0, cb1));
                  brrest = trim(brrest.substr(cb1 + 1));
                  size_t cb2 = brrest.find(',');
                  if (cb2 == std::string::npos) continue;
                  std::string zero = trim(brrest.substr(0, cb2));
                  // std::string bodyLabel = trim(brrest.substr(cb2 + 1));
                  
                  if (r != condReg || zero != "x0") continue;
                  
                  // Look for jal x0, exit on next line
                  for (size_t n = m + 1; n < result.size() && n <= m + 2; ++n) {
                    std::string v = trim(result[n]);
                    if (v.empty()) continue;
                    if (startsWith(v, "jal x0, ") || startsWith(v, "j ")) {
                      std::string lab;
                      if (startsWith(v, "jal x0, ")) lab = trim(v.substr(8));
                      else lab = trim(v.substr(2));
                      beqzIdx = n;  // Use jal as the branch index
                      exitLabel = lab;
                      break;
                    }
                    break;
                  }
                }
                if (beqzIdx != SIZE_MAX) break;
              }
              break;
            }
          }
        }
        if (bound > 0 && beqzIdx != SIZE_MAX) break;
      }
    }
    
    if (sltiIdx == SIZE_MAX || beqzIdx == SIZE_MAX) continue;
    
    // Find back-jump "j header" or "jal x0, header"
    size_t jIdx = SIZE_MAX;
    for (size_t k = beqzIdx + 1; k < result.size() && k <= beqzIdx + 80; ++k) {
      std::string t = trim(result[k]);
      if (isLabelLine(result[k])) {
        // Allow one body label right after beqz
        bool onlyBlankBetween = true;
        for (size_t u = beqzIdx + 1; u < k; ++u) {
          if (!trim(result[u]).empty()) { onlyBlankBetween = false; break; }
        }
        if (!onlyBlankBetween) { jIdx = SIZE_MAX; break; }
        continue;
      }
      std::string dst;
      if (startsWith(t, "j ") && !startsWith(t, "jal")) {
        dst = trim(t.substr(2));
      } else if (startsWith(t, "jal x0, ")) {
        dst = trim(t.substr(8));
      }
      if (!dst.empty() && dst == header) {
        jIdx = k;
        break;
      }
    }
    
    if (jIdx == SIZE_MAX) continue;
    
    // Find IV update patterns:
    // Pattern 1: addi tmp, iv, 1; mv iv, tmp
    // Pattern 2: addi one, x0, 1; add tmp, iv, one; addi iv, tmp, 0
    std::string ivReg, tmpReg;
    for (size_t k = jIdx; k-- > beqzIdx + 1; ) {
      std::string t = trim(result[k]);
      
      // Pattern 1: mv iv, tmp
      if (startsWith(t, "mv ")) {
        std::string rest = trim(t.substr(3));
        size_t c = rest.find(',');
        if (c == std::string::npos) continue;
        std::string dst = trim(rest.substr(0, c));
        std::string src = trim(rest.substr(c + 1));
        
        // Look for preceding addi src, ?, 1
        for (size_t p = k; p-- > beqzIdx + 1; ) {
          std::string u = trim(result[p]);
          if (!startsWith(u, "addi ")) continue;
          std::string r = trim(u.substr(5));
          size_t c1 = r.find(',');
          if (c1 == std::string::npos) continue;
          std::string rd = trim(r.substr(0, c1));
          r = trim(r.substr(c1 + 1));
          size_t c2 = r.find(',');
          if (c2 == std::string::npos) continue;
          std::string rs = trim(r.substr(0, c2));
          std::string immS = trim(r.substr(c2 + 1));
          if (immS != "1") continue;
          if (rd == src) {
            ivReg = dst;
            tmpReg = rd;
            break;
          }
        }
        if (!ivReg.empty()) break;
      }
      
      // Pattern 2: addi iv, tmp, 0 (acts like mv)
      if (startsWith(t, "addi ")) {
        std::string rest = trim(t.substr(5));
        size_t c1 = rest.find(',');
        if (c1 == std::string::npos) continue;
        std::string dst = trim(rest.substr(0, c1));
        rest = trim(rest.substr(c1 + 1));
        size_t c2 = rest.find(',');
        if (c2 == std::string::npos) continue;
        std::string src = trim(rest.substr(0, c2));
        std::string immS = trim(rest.substr(c2 + 1));
        if (immS != "0") continue;
        
        // Look for add src, ?, oneReg where oneReg = 1
        for (size_t p = k; p-- > beqzIdx + 1; ) {
          std::string u = trim(result[p]);
          if (!startsWith(u, "add ")) continue;
          std::string r = trim(u.substr(4));
          size_t a1 = r.find(',');
          if (a1 == std::string::npos) continue;
          std::string addRd = trim(r.substr(0, a1));
          r = trim(r.substr(a1 + 1));
          size_t a2 = r.find(',');
          if (a2 == std::string::npos) continue;
          std::string addRs1 = trim(r.substr(0, a2));
          std::string addRs2 = trim(r.substr(a2 + 1));
          
          if (addRd != src) continue;
          
          // Check if one of the sources is the IV and the other is 1
          std::string maybeIv = addRs1;
          std::string maybeOne = addRs2;
          
          // Look for addi maybeOne, x0, 1
          for (size_t q = p; q-- > beqzIdx + 1; ) {
            std::string v = trim(result[q]);
            if (!startsWith(v, "addi ")) continue;
            std::string vr = trim(v.substr(5));
            size_t b1 = vr.find(',');
            if (b1 == std::string::npos) continue;
            std::string vRd = trim(vr.substr(0, b1));
            vr = trim(vr.substr(b1 + 1));
            size_t b2 = vr.find(',');
            if (b2 == std::string::npos) continue;
            std::string vRs = trim(vr.substr(0, b2));
            std::string vImm = trim(vr.substr(b2 + 1));
            
            if (vRd == maybeOne && vRs == "x0" && vImm == "1") {
              ivReg = dst;
              tmpReg = src;
              break;
            }
          }
          if (!ivReg.empty()) break;
        }
        if (!ivReg.empty()) break;
      }
    }
    
    if (ivReg.empty() || tmpReg.empty()) continue;
    
    // Verify IV not used elsewhere in loop body (except for IV update control)
    // Find where IV update starts (add with ivReg as source)
    size_t ivUpdateStart = jIdx;
    for (size_t k = beqzIdx + 1; k < jIdx; ++k) {
      std::string t = trim(result[k]);
      if (startsWith(t, "add ")) {
        // Check if this is "add tmpReg, ivReg, ..." or "add tmpReg, ..., ivReg"
        std::string rest = trim(t.substr(4));
        size_t c1 = rest.find(',');
        if (c1 == std::string::npos) continue;
        std::string rd = trim(rest.substr(0, c1));
        rest = trim(rest.substr(c1 + 1));
        size_t c2 = rest.find(',');
        if (c2 == std::string::npos) continue;
        std::string rs1 = trim(rest.substr(0, c2));
        std::string rs2 = trim(rest.substr(c2 + 1));
        if ((rs1 == ivReg || rs2 == ivReg) && rd == tmpReg) {
          ivUpdateStart = k;
          break;
        }
      }
    }
    
    bool ivUsed = false;
    for (size_t k = beqzIdx + 1; k < ivUpdateStart; ++k) {
      std::string t = trim(result[k]);
      if (startsWith(t, "addi ") || startsWith(t, "mv ")) continue;
      if (isLabelLine(result[k])) continue;
      if (containsWord(result[k], ivReg)) { ivUsed = true; break; }
    }
    if (ivUsed) continue;
    
    // Find IV init to 0 before header:
    // Pattern 1: li r, 0; mv ivReg, r
    // Pattern 2: addi ivReg, x0, 0
    bool hasInit0 = false;
    for (size_t k = i; k > 0 && k > i - 15; --k) {
      std::string t = trim(result[k - 1]);
      
      // Pattern 2: addi ivReg, x0, 0
      if (startsWith(t, "addi ")) {
        std::string rest = trim(t.substr(5));
        size_t c1 = rest.find(',');
        if (c1 == std::string::npos) continue;
        std::string rd = trim(rest.substr(0, c1));
        rest = trim(rest.substr(c1 + 1));
        size_t c2 = rest.find(',');
        if (c2 == std::string::npos) continue;
        std::string rs = trim(rest.substr(0, c2));
        std::string immS = trim(rest.substr(c2 + 1));
        if (rd == ivReg && rs == "x0" && immS == "0") {
          hasInit0 = true;
          break;
        }
      }
      
      // Pattern 1: li r, 0
      if (startsWith(t, "li ")) {
        std::string rest = trim(t.substr(3));
        size_t c = rest.find(',');
        if (c == std::string::npos) continue;
        std::string rd = trim(rest.substr(0, c));
        std::string immS = trim(rest.substr(c + 1));
        if (immS != "0") continue;
        for (size_t m = k; m < i; ++m) {
          std::string u = trim(result[m]);
          if (!startsWith(u, "mv ")) continue;
          std::string rr = trim(u.substr(3));
          size_t c2 = rr.find(',');
          if (c2 == std::string::npos) continue;
          std::string dst = trim(rr.substr(0, c2));
          std::string src = trim(rr.substr(c2 + 1));
          if (dst == ivReg && src == rd) { hasInit0 = true; break; }
        }
        if (hasInit0) break;
      }
    }
    if (!hasInit0) continue;
    
    // Extract loop body core (between beqz and IV update)
    size_t tailStart = jIdx;
    for (size_t k = beqzIdx + 1; k < jIdx; ++k) {
      std::string t = trim(result[k]);
      if (startsWith(t, "addi ") && containsWord(t, tmpReg) && t.find(", 1") != std::string::npos) {
        tailStart = k;
        break;
      }
    }
    
    if (tailStart <= beqzIdx + 1) continue;
    
    std::vector<std::string> coreBody;
    for (size_t k = beqzIdx + 1; k < tailStart; ++k) {
      std::string t = trim(result[k]);
      if (t.empty()) continue;
      if (isLabelLine(result[k])) continue;  // Skip body labels
      if (startsWith(t, "beqz ") || startsWith(t, "j ")) { coreBody.clear(); break; }
      coreBody.push_back(result[k]);
    }
    if (coreBody.empty()) continue;
    
    // Build unrolled code
    std::vector<std::string> repl;
    repl.push_back(result[i]);  // Keep header label
    for (int rep = 0; rep < bound; ++rep) {
      repl.insert(repl.end(), coreBody.begin(), coreBody.end());
    }
    repl.push_back("\tli " + ivReg + ", " + std::to_string(bound));
    repl.push_back("\tj " + exitLabel);
    
    // Apply replacement
    result.erase(result.begin() + static_cast<long>(i), 
                 result.begin() + static_cast<long>(jIdx + 1));
    result.insert(result.begin() + static_cast<long>(i), repl.begin(), repl.end());
    anyChange = true;
    i += repl.size();
  }
  
  return result;
}

// Remove dead code after unconditional jumps
std::vector<std::string> optimizeDeadCodeAfterJump(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  result.reserve(lines.size());
  bool skipping = false;
  
  for (size_t i = 0; i < lines.size(); ++i) {
    if (isLabelLine(lines[i])) {
      skipping = false;
      result.push_back(lines[i]);
      continue;
    }
    
    if (skipping) continue;
    
    std::string t = trim(lines[i]);
    if (startsWith(t, "j ") && !startsWith(t, "jal")) {
      result.push_back(lines[i]);
      skipping = true;
      continue;
    }
    
    result.push_back(lines[i]);
  }
  
  return result;
}

// Remove unused labels and redundant jumps
std::vector<std::string> optimizeUnusedLabels(const std::vector<std::string>& lines) {
  // Collect all referenced labels
  std::set<std::string> refs;
  std::set<std::string> globalSymbols;
  for (const auto& line : lines) {
    std::string t = trim(line);
    if (startsWith(t, ".globl ")) {
      globalSymbols.insert(trim(t.substr(7)));
      continue;
    }
    if (startsWith(t, "j ")) {
      refs.insert(trim(t.substr(2)));
    } else if (startsWith(t, "jal x0, ")) {
      refs.insert(trim(t.substr(8)));
    } else if (startsWith(t, "beqz ") || startsWith(t, "bne ") || startsWith(t, "beq ")) {
      size_t c = t.find(',');
      if (c != std::string::npos) {
        std::string rest = trim(t.substr(c + 1));
        size_t c2 = rest.find(',');
        if (c2 != std::string::npos) rest = trim(rest.substr(c2 + 1));
        refs.insert(rest);
      }
    } else if (startsWith(t, "jal ra, ") || startsWith(t, "jal ")) {
      // Function calls - keep the target
      size_t space = t.find(' ');
      if (space != std::string::npos) {
        std::string rest = trim(t.substr(space + 1));
        size_t c = rest.find(',');
        if (c != std::string::npos) rest = trim(rest.substr(c + 1));
        refs.insert(rest);
      }
    }
  }
  
  std::vector<std::string> result;
  result.reserve(lines.size());
  
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string lab;
    if (isLabelLine(lines[i])) {
      std::string t = trim(lines[i]);
      lab = t.substr(0, t.size() - 1);
      
      // Keep exported symbols and referenced labels.
      if (globalSymbols.find(lab) != globalSymbols.end()) {
        result.push_back(lines[i]);
        continue;
      }

      // Drop only compiler-generated internal control-flow labels.
      bool isAuto = (!lab.empty() && lab[0] == 'L' && lab.size() > 1 && std::isdigit(lab[1])) ||
                    (lab.find("_B") != std::string::npos) ||
                    (lab.find("_while_") != std::string::npos) ||
                    (lab.find("_if_true") != std::string::npos) ||
                    (lab.find("_if_false") != std::string::npos) ||
                    (lab.find("_if_end") != std::string::npos) ||
                    (lab.find("_for_") != std::string::npos);
      
      if (isAuto && refs.find(lab) == refs.end()) {
        continue;  // Drop unreferenced auto labels
      }
    }
    
    result.push_back(lines[i]);
  }
  
  return result;
}

// Reduce repeated accumulation patterns: acc = acc + inv (N times) -> acc = acc + inv*N
std::vector<std::string> optimizeUnrolledTinyAccum(const std::vector<std::string>& lines) {
  std::vector<std::string> result = lines;
  
  auto parseAdd = [](const std::string& line, std::string* rd, std::string* a, std::string* b) -> bool {
    std::string t = trim(line);
    if (!startsWith(t, "add ")) return false;
    std::string rest = trim(t.substr(4));
    size_t c1 = rest.find(',');
    if (c1 == std::string::npos) return false;
    std::string d = trim(rest.substr(0, c1));
    rest = trim(rest.substr(c1 + 1));
    size_t c2 = rest.find(',');
    if (c2 == std::string::npos) return false;
    std::string x = trim(rest.substr(0, c2));
    std::string y = trim(rest.substr(c2 + 1));
    if (rd) *rd = d;
    if (a) *a = x;
    if (b) *b = y;
    return true;
  };
  
  auto parseLiMul = [](const std::string& line, std::string* rd, std::string* rs, std::string* imm) -> bool {
    std::string t = trim(line);
    if (!startsWith(t, "mul ")) return false;
    std::string rest = trim(t.substr(4));
    size_t c1 = rest.find(',');
    if (c1 == std::string::npos) return false;
    *rd = trim(rest.substr(0, c1));
    rest = trim(rest.substr(c1 + 1));
    size_t c2 = rest.find(',');
    if (c2 == std::string::npos) return false;
    *rs = trim(rest.substr(0, c2));
    *imm = trim(rest.substr(c2 + 1));
    return true;
  };
  
  // Look for sequences of adds with same operands
  for (size_t i = 0; i + 3 < result.size(); ++i) {
    std::string rd1, a1, b1;
    if (!parseAdd(result[i], &rd1, &a1, &b1)) continue;
    
    // Check for same pattern repeated
    int reps = 1;
    size_t j = i + 1;
    while (j < result.size() && reps < 16) {
      // Skip labels or breaks
      std::string t = trim(result[j]);
      if (isLabelLine(result[j])) break;
      if (startsWith(t, "beqz ") || startsWith(t, "j ")) break;
      
      std::string rd2, a2, b2;
      if (!parseAdd(result[j], &rd2, &a2, &b2)) {
        j++;
        continue;
      }
      
      // Must be same pattern: add rd, rd, inv or add rd, inv, rd
      bool samePattern = (rd2 == rd1) && 
        (((a2 == rd1) && (b2 == b1)) || ((b2 == rd1) && (a2 == b1)));
      
      if (!samePattern) break;
      
      reps++;
      j++;
    }
    
    // If we found repeated adds and count > 2, try to reduce
    // This is complex - for now just skip if reps <= 2
    if (reps > 2) {
      // Could replace with mul, but need to be careful about register allocation
      // For now, just mark as potential optimization point
    }
  }
  
  return result;
}

// Optimize li followed by mv to single li
std::vector<std::string> optimizeLiMv(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  result.reserve(lines.size());

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string t = trim(lines[i]);
    if (isFloatAsmLine(t)) {
      result.push_back(lines[i]);
      continue;
    }

    // Look for li t0, imm followed by mv rd, t0
    if (startsWith(t, "li ")) {
      std::string rest = trim(t.substr(3));
      size_t c = rest.find(',');
      if (c != std::string::npos) {
        std::string rd = trim(rest.substr(0, c));
        std::string imm = trim(rest.substr(c + 1));

        // Check if rd is a temp and next is mv dest, rd
        if ((rd == "t0" || rd == "t1" || rd == "t5") && i + 1 < lines.size()) {
          std::string next = trim(lines[i + 1]);
          if (isFloatAsmLine(next)) {
            result.push_back(lines[i]);
            continue;
          }
          if (startsWith(next, "mv ")) {
            std::string mvRest = trim(next.substr(3));
            size_t mc = mvRest.find(',');
            if (mc != std::string::npos) {
              std::string mvDest = trim(mvRest.substr(0, mc));
              std::string mvSrc = trim(mvRest.substr(mc + 1));

              if (mvSrc == rd && mvDest != rd) {
                // Replace with single li mvDest, imm
                result.push_back("\tli " + mvDest + ", " + imm);
                i++;  // Skip the mv
                continue;
              }
            }
          }
        }
      }
    }

    result.push_back(lines[i]);
  }

  return result;
}

std::vector<std::string> peepholeOptimize(const std::vector<std::string>& asmLines) {
  std::vector<std::string> result = asmLines;
  
  bool changed = true;
  int passes = 0;
  while (changed && passes < 20) {
    changed = false;
    passes++;
    
    size_t prevSize = result.size();
    result = optimizeMoveChains(result);
    if (result.size() != prevSize) changed = true;
    
    prevSize = result.size();
    result = optimizeLoadStore(result);
    if (result.size() != prevSize) changed = true;
    
    prevSize = result.size();
    result = optimizeRedundantOps(result);
    if (result.size() != prevSize) changed = true;
    
    prevSize = result.size();
    result = optimizeJumpToNext(result);
    if (result.size() != prevSize) changed = true;
    
    // Remove unused labels
    prevSize = result.size();
    result = optimizeUnusedLabels(result);
    if (result.size() != prevSize) changed = true;
    
    // Small loop unrolling (assembly level)
    prevSize = result.size();
    result = optimizeSmallConstLoops(result);
    if (result.size() != prevSize) changed = true;
    
    // Li+mv -> li optimization
    prevSize = result.size();
    result = optimizeLiMv(result);
    if (result.size() != prevSize) changed = true;
    
    prevSize = result.size();
    result = optimizeDeadCodeAfterJump(result);
    if (result.size() != prevSize) changed = true;
  }
  
  return result;
}

}  // namespace

std::string CodeGen::binOpMnemonic(BinaryOp op, ValueType type) const {
  bool isW = (type == ValueType::I32);
  std::string w = isW ? "w" : "";
  switch (op) {
    case BinaryOp::Add:
      return "add" + w;
    case BinaryOp::Sub:
      return "sub" + w;
    case BinaryOp::Mul:
      return "mul" + w;
    case BinaryOp::Div:
      return "div" + w;
    case BinaryOp::Mod:
      return "rem" + w;
    case BinaryOp::And:
      return "and";
    case BinaryOp::Or:
      return "or";
    case BinaryOp::Lt:
      return "slt";
    case BinaryOp::Gt:
      return "sgt";
    case BinaryOp::Le:
      return "sle";
    case BinaryOp::Ge:
      return "sge";
    case BinaryOp::Eq:
      return "seq";
    case BinaryOp::Ne:
      return "sne";
  }
  return "unknown";
}

std::string CodeGen::renderOperandWithAlloc(
    const Operand& op, const std::unordered_map<int, int>& allocation,
    const StackFrame& frame) const {
  if (op.isImm()) {
    if (isFloatValueType(op.valueType)) {
      std::ostringstream os;
      os << op.immFloatValue << "f";
      return os.str();
    }
    return std::to_string(op.immValue);
  }
  if (op.isVReg()) {
    auto it = allocation.find(op.vregId);
    if (it != allocation.end()) {
      return RISCVRegMap::physicalRegName(it->second);
    }
    auto spillIt = frame.spillSlots.find(op.vregId);
    if (spillIt != frame.spillSlots.end()) {
      return std::to_string(spillIt->second) + "(sp)";
    }
    return "%v" + std::to_string(op.vregId);
  }
  if (op.isStackPtr()) {
    return "sp";
  }
  return op.globalName;
}

bool CodeGen::isInt12(int imm) const { return imm >= -2048 && imm <= 2047; }

bool CodeGen::isFloatValueType(ValueType type) const {
  return type == ValueType::F32;
}

std::string CodeGen::floatCompareMnemonic(BinaryOp op) const {
  switch (op) {
    case BinaryOp::Lt:
      return "flt.s";
    case BinaryOp::Le:
      return "fle.s";
    case BinaryOp::Eq:
      return "feq.s";
    default:
      return "";
  }
}

int CodeGen::floatBits(float value) const {
  int bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float/int size mismatch");
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}

void CodeGen::emitLoadFloatImmediate(const std::string& freg, float value,
                                     std::vector<std::string>& out) const {
  emitLoadImmediate("t6", floatBits(value), out);
  out.push_back("\tfmv.w.x " + freg + ", t6");
}

void CodeGen::emitLoadGlobalAddress(const std::string& dstReg,
                                    const std::string& symbol,
                                    std::vector<std::string>& out) const {
  std::string anchor = ".Lpcrel_" + std::to_string(gPcrelLabelCounter++);
  out.push_back(anchor + ":");
  out.push_back("\tauipc " + dstReg + ", %pcrel_hi(" + symbol + ")");
  out.push_back("\taddi " + dstReg + ", " + dstReg + ", %pcrel_lo(" + anchor +
                ")");
}

void CodeGen::emitLoadImmediate(const std::string& reg, int imm,
                                std::vector<std::string>& out) const {
  if (isInt12(imm)) {
    out.push_back("\taddi " + reg + ", x0, " + std::to_string(imm));
    return;
  }

  out.push_back("\tli " + reg + ", " + std::to_string(imm));
}

void CodeGen::emitStackAddress(const std::string& dstReg,
                               const std::string& baseReg, int offset,
                               std::vector<std::string>& out) const {
  if (isInt12(offset)) {
    out.push_back("\taddi " + dstReg + ", " + baseReg + ", " +
                  std::to_string(offset));
    return;
  }

  emitLoadImmediate(dstReg, offset, out);
  out.push_back("\tadd " + dstReg + ", " + baseReg + ", " + dstReg);
}

void CodeGen::emitStackLoad(const std::string& dstReg, int offset,
                            std::vector<std::string>& out,
                            const std::string& addrScratch) const {
  if (isInt12(offset)) {
    out.push_back("\tlw " + dstReg + ", " + std::to_string(offset) + "(sp)");
    return;
  }

  if (dstReg == addrScratch) {
    out.push_back("\t# invalid stack load register overlap");
  }
  emitStackAddress(addrScratch, "sp", offset, out);
  out.push_back("\tlw " + dstReg + ", 0(" + addrScratch + ")");
}

void CodeGen::emitStackStore(const std::string& srcReg, int offset,
                             std::vector<std::string>& out,
                             const std::string& addrScratch) const {
  if (isInt12(offset)) {
    out.push_back("\tsw " + srcReg + ", " + std::to_string(offset) + "(sp)");
    return;
  }

  emitStackAddress(addrScratch, "sp", offset, out);
  out.push_back("\tsw " + srcReg + ", 0(" + addrScratch + ")");
}

void CodeGen::emitStackLoad64(const std::string& dstReg, int offset,
                              std::vector<std::string>& out,
                              const std::string& addrScratch) const {
  if (isInt12(offset)) {
    out.push_back("\tld " + dstReg + ", " + std::to_string(offset) + "(sp)");
    return;
  }
  emitStackAddress(addrScratch, "sp", offset, out);
  out.push_back("\tld " + dstReg + ", 0(" + addrScratch + ")");
}

void CodeGen::emitStackStore64(const std::string& srcReg, int offset,
                               std::vector<std::string>& out,
                               const std::string& addrScratch) const {
  if (isInt12(offset)) {
    out.push_back("\tsd " + srcReg + ", " + std::to_string(offset) + "(sp)");
    return;
  }
  emitStackAddress(addrScratch, "sp", offset, out);
  out.push_back("\tsd " + srcReg + ", 0(" + addrScratch + ")");
}

void CodeGen::emitAdjustSP(int delta, std::vector<std::string>& out) const {
  if (delta == 0) return;
  if (isInt12(delta)) {
    out.push_back("\taddi sp, sp, " + std::to_string(delta));
    return;
  }

  emitLoadImmediate("t6", delta, out);
  out.push_back("\tadd sp, sp, t6");
}

CodeGen::StackFrame CodeGen::computeStackFrame(
    const IRFunction& fn, const RegAllocResult& allocResult,
    const LivenessResult& liveness) {
  // Collect all virtual registers that appear anywhere in the function so we
  // can provide a stack slot for any value the allocator left uncolored.
  std::set<int> allVRegs;
  auto trackOp = [&](const Operand& op) {
    if (op.isVReg()) allVRegs.insert(op.vregId);
  };
  for (int v : fn.params) allVRegs.insert(v);
  for (const auto& inst : fn.instructions) {
    switch (inst->kind) {
      case InstKind::Binary: {
        auto* b = static_cast<const BinaryInst*>(inst.get());
        allVRegs.insert(b->dest);
        trackOp(b->lhs);
        trackOp(b->rhs);
        break;
      }
      case InstKind::Unary: {
        auto* u = static_cast<const UnaryInst*>(inst.get());
        allVRegs.insert(u->dest);
        trackOp(u->operand);
        break;
      }
      case InstKind::Copy: {
        auto* c = static_cast<const CopyInst*>(inst.get());
        allVRegs.insert(c->dest);
        trackOp(c->src);
        break;
      }
      case InstKind::Load: {
        auto* l = static_cast<const LoadInst*>(inst.get());
        allVRegs.insert(l->dest);
        trackOp(l->addr);
        break;
      }
      case InstKind::Store: {
        auto* s = static_cast<const StoreInst*>(inst.get());
        trackOp(s->src);
        trackOp(s->addr);
        break;
      }
      case InstKind::Branch: {
        auto* br = static_cast<const BranchInst*>(inst.get());
        trackOp(br->cond);
        break;
      }
      case InstKind::Call: {
        auto* c = static_cast<const CallInst*>(inst.get());
        if (c->hasDest) allVRegs.insert(c->dest);
        for (const auto& a : c->args) trackOp(a);
        break;
      }
      case InstKind::Return: {
        auto* r = static_cast<const ReturnInst*>(inst.get());
        if (r->hasValue) trackOp(r->value);
        break;
      }
      case InstKind::Label:
      case InstKind::Jump:
        break;
    }
  }

  StackFrame frame{};
  frame.frameSize = 0;
  frame.localVarOffset = 0;
  frame.spillAreaSize = 0;
  frame.spillAreaOffset = 0;
  frame.callerSavedOffset = 0;
  frame.outgoingArgOffset = 0;
  frame.raOffset = 0;
  frame.savedRegsOffset = 0;
  frame.maxOutgoingArgs = 0;

  // Determine callee-saved registers we used (s0-s3 colors 5-8).
  for (const auto& entry : allocResult.allocation) {
    int color = entry.second;
    if (color >= 5) {
      std::string regName = RISCVRegMap::physicalRegName(color);
      if (std::find(frame.savedRegs.begin(), frame.savedRegs.end(), regName) ==
          frame.savedRegs.end()) {
        frame.savedRegs.push_back(regName);
      }
    }
  }

  std::sort(frame.savedRegs.begin(), frame.savedRegs.end());

  // Track maximum stack arguments required under mixed int/float calling convention.
  int maxStackArgs = 0;
  for (const auto& inst : fn.instructions) {
    if (inst->kind != InstKind::Call) continue;
    auto* c = static_cast<const CallInst*>(inst.get());
    int intRegCount = 0;
    int floatRegCount = 0;
    int stackCount = 0;
    for (size_t i = 0; i < c->args.size(); ++i) {
      ValueType argType = i < c->argTypes.size() ? c->argTypes[i] : ValueType::I32;
      classifyArgLocation(argType, intRegCount, floatRegCount, stackCount);
    }
    if (stackCount > maxStackArgs) {
      maxStackArgs = stackCount;
    }
  }
  frame.maxOutgoingArgs = maxStackArgs;

  // Caller-saved registers that are live across calls need spill slots.
  std::set<std::string> callerSavedSet;
  for (size_t i = 0; i < fn.instructions.size(); ++i) {
    if (fn.instructions[i]->kind != InstKind::Call) continue;
    const auto& liveOutSet = liveness.liveOut[i];
    for (int vreg : liveOutSet) {
      auto it = allocResult.allocation.find(vreg);
      if (it == allocResult.allocation.end()) continue;
      int color = it->second;
      if (color >= 0 && color <= 4) {  // caller-saved t0-t4
        callerSavedSet.insert(RISCVRegMap::physicalRegName(color));
      }
    }
  }
  frame.callerSavedRegs.assign(callerSavedSet.begin(), callerSavedSet.end());

  // Spill slots for spilled virtual registers, and for any non-parameter vreg
  // that failed to receive a color. Parameters keep a stable input slot plus a
  // separate current-value slot so later writes do not clobber the original
  // incoming argument copy.
  int spillOffset = 0;
  int paramSlotOffset = 0;
  for (size_t i = 0; i < fn.params.size(); ++i) {
    int vreg = fn.params[i];
    frame.paramIndexByVReg[vreg] = static_cast<int>(i);
    frame.paramSlots[vreg] = paramSlotOffset;
    paramSlotOffset += 8;  // 8 bytes per param slot (64-bit register)
    if (allocResult.allocation.find(vreg) == allocResult.allocation.end()) {
      frame.paramValueSlots[vreg] = paramSlotOffset;
      paramSlotOffset += 8;  // 8 bytes per param value slot
    }
  }
  for (int vreg : allocResult.spilledVRegs) {
    if (frame.paramSlots.find(vreg) != frame.paramSlots.end()) continue;
    frame.spillSlots[vreg] = paramSlotOffset + spillOffset;
    spillOffset += 8;  // 8 bytes per spill slot (64-bit)
  }
  for (int vreg : allVRegs) {
    bool isParam = frame.paramSlots.find(vreg) != frame.paramSlots.end();
    bool hasColor =
        allocResult.allocation.find(vreg) != allocResult.allocation.end();
    bool alreadySpilled = frame.spillSlots.find(vreg) != frame.spillSlots.end();
    if (!isParam && !hasColor && !alreadySpilled) {
      frame.spillSlots[vreg] = paramSlotOffset + spillOffset;
      spillOffset += 8;  // 8 bytes per spill slot (64-bit)
    }
  }
  frame.spillAreaSize = paramSlotOffset + spillOffset;

  int offset = 0;

  // Outgoing stack arguments area MUST be at the bottom (lowest addresses)
  // of the frame, so callee can find them at sp + calleeFrameSize.
  frame.outgoingArgOffset = offset;
  offset += frame.maxOutgoingArgs * 8;  // 8 bytes per stack arg (lp64d ABI)

  // Spill area (for vregs spilled by allocator).
  frame.spillAreaOffset = offset;
  offset += frame.spillAreaSize;

  // Caller-saved spill area (for regs live across calls).
  frame.callerSavedOffset = offset;
  for (const auto& reg : frame.callerSavedRegs) {
    frame.callerSavedSlots[reg] = offset;
    offset += 8;  // 8 bytes for 64-bit registers
  }

  // Save area for ra and callee-saved.
  frame.raOffset = offset;
  offset += 8;  // 8 bytes for 64-bit ra

  frame.savedRegsOffset = offset;
  offset += static_cast<int>(frame.savedRegs.size()) * 8;  // 8 bytes per register

  frame.localVarOffset = offset;

  // 为局部数组分配额外的栈空间
  offset += fn.localArraySize;

  frame.frameSize = ((offset + 15) / 16) * 16;

  // Adjust spill slot offsets to absolute (from sp).
  for (auto& kv : frame.spillSlots) {
    kv.second += frame.spillAreaOffset;
  }
  for (auto& kv : frame.paramSlots) {
    kv.second += frame.spillAreaOffset;
  }
  for (auto& kv : frame.paramValueSlots) {
    kv.second += frame.spillAreaOffset;
  }

  return frame;
}

void CodeGen::emitPrologue(const IRFunction& fn, const StackFrame& frame,
                           std::vector<std::string>& out) {
  // Use a per-function entry label to avoid duplicate symbol definitions.
  std::string entryLabel = fn.name + "_entry";

  out.push_back(".globl " + fn.name);
  out.push_back(fn.name + ":");
  out.push_back("prologue_" + fn.name + ":");

  if (frame.frameSize == 0) {
    out.push_back(entryLabel + ":");
    return;
  }

  emitAdjustSP(-frame.frameSize, out);
  emitStackStore64("ra", frame.raOffset, out, "t6");

  int offset = frame.savedRegsOffset;
  for (const auto& reg : frame.savedRegs) {
    emitStackStore64(reg, offset, out, "t6");
    offset += 8;
  }

  out.push_back(entryLabel + ":");
}

void CodeGen::emitEpilogue(const IRFunction& fn, const StackFrame& frame,
                           std::vector<std::string>& out) {
  if (frame.frameSize == 0) {
    out.push_back("\tret");
    return;
  }

  int offset = frame.savedRegsOffset;
  for (const auto& reg : frame.savedRegs) {
    emitStackLoad64(reg, offset, out, "t6");
    offset += 8;
  }

  emitStackLoad64("ra", frame.raOffset, out, "t6");
  emitAdjustSP(frame.frameSize, out);
  out.push_back("\tret");
}

std::string CodeGen::renderInstructionWithAlloc(
    const Instruction* inst, const std::unordered_map<int, int>& allocation,
    const StackFrame& frame) const {
  std::ostringstream os;

  switch (inst->kind) {
    case InstKind::Binary: {
      auto* b = static_cast<const BinaryInst*>(inst);
      auto it = allocation.find(b->dest);
      std::string destReg = (it != allocation.end())
                                ? RISCVRegMap::physicalRegName(it->second)
                                : "%v" + std::to_string(b->dest);

      if (isFloatValueType(b->operandType)) {
        if (isFloatValueType(b->resultType)) {
          switch (b->op) {
            case BinaryOp::Add:
              os << "fadd.s ft?, " << renderOperandWithAlloc(b->lhs, allocation, frame)
                 << ", " << renderOperandWithAlloc(b->rhs, allocation, frame)
                 << " -> " << destReg;
              break;
            case BinaryOp::Sub:
              os << "fsub.s ft?, " << renderOperandWithAlloc(b->lhs, allocation, frame)
                 << ", " << renderOperandWithAlloc(b->rhs, allocation, frame)
                 << " -> " << destReg;
              break;
            case BinaryOp::Mul:
              os << "fmul.s ft?, " << renderOperandWithAlloc(b->lhs, allocation, frame)
                 << ", " << renderOperandWithAlloc(b->rhs, allocation, frame)
                 << " -> " << destReg;
              break;
            case BinaryOp::Div:
              os << "fdiv.s ft?, " << renderOperandWithAlloc(b->lhs, allocation, frame)
                 << ", " << renderOperandWithAlloc(b->rhs, allocation, frame)
                 << " -> " << destReg;
              break;
            default:
              os << "fbin(?) " << destReg;
              break;
          }
        } else {
          os << floatCompareMnemonic(b->op) << " " << destReg << ", "
             << renderOperandWithAlloc(b->lhs, allocation, frame) << ", "
             << renderOperandWithAlloc(b->rhs, allocation, frame);
        }
        break;
      }

      os << binOpMnemonic(b->op, b->operandType) << " " << destReg << ", "
         << renderOperandWithAlloc(b->lhs, allocation, frame) << ", "
         << renderOperandWithAlloc(b->rhs, allocation, frame);
      break;
    }
    case InstKind::Unary: {
      auto* u = static_cast<const UnaryInst*>(inst);
      auto it = allocation.find(u->dest);
      std::string destReg = (it != allocation.end())
                                ? RISCVRegMap::physicalRegName(it->second)
                                : "%v" + std::to_string(u->dest);

      if (isFloatValueType(u->operandType)) {
        switch (u->op) {
          case UnaryOp::Neg:
            os << "fneg.s ft?, " << renderOperandWithAlloc(u->operand, allocation, frame)
               << " -> " << destReg;
            break;
          case UnaryOp::Not:
            os << "fnot(cond) " << destReg << ", "
               << renderOperandWithAlloc(u->operand, allocation, frame);
            break;
          case UnaryOp::Plus:
            os << "fmv.s/copy " << destReg << ", "
               << renderOperandWithAlloc(u->operand, allocation, frame);
            break;
        }
        break;
      }

      switch (u->op) {
        case UnaryOp::Neg:
          os << "neg " << destReg << ", "
             << renderOperandWithAlloc(u->operand, allocation, frame);
          break;
        case UnaryOp::Not:
          os << "seqz " << destReg << ", "
             << renderOperandWithAlloc(u->operand, allocation, frame);
          break;
        case UnaryOp::Plus:
          os << "mv " << destReg << ", "
             << renderOperandWithAlloc(u->operand, allocation, frame);
          break;
      }
      break;
    }
    case InstKind::Copy: {
      auto* c = static_cast<const CopyInst*>(inst);
      auto it = allocation.find(c->dest);
      std::string destReg = (it != allocation.end())
                                ? RISCVRegMap::physicalRegName(it->second)
                                : "%v" + std::to_string(c->dest);

      if (isFloatValueType(c->destType) && !isFloatValueType(c->src.valueType)) {
        os << "fcvt.s.w " << destReg << ", "
           << renderOperandWithAlloc(c->src, allocation, frame);
      } else if (!isFloatValueType(c->destType) && isFloatValueType(c->src.valueType)) {
        os << "fcvt.w.s " << destReg << ", "
           << renderOperandWithAlloc(c->src, allocation, frame);
      } else {
        os << "mv " << destReg << ", "
           << renderOperandWithAlloc(c->src, allocation, frame);
      }
      break;
    }
    case InstKind::Branch: {
      auto* br = static_cast<const BranchInst*>(inst);
      os << "branch " << renderOperandWithAlloc(br->cond, allocation, frame)
         << " ? " << br->trueLabel << " : " << br->falseLabel;
      break;
    }
    case InstKind::Jump: {
      auto* j = static_cast<const JumpInst*>(inst);
      os << "j " << j->target;
      break;
    }
    case InstKind::Call: {
      auto* c = static_cast<const CallInst*>(inst);
      if (c->hasDest) {
        auto it = allocation.find(c->dest);
        std::string destReg = (it != allocation.end())
                                  ? RISCVRegMap::physicalRegName(it->second)
                                  : "%v" + std::to_string(c->dest);
        os << destReg << " = ";
      }
      os << "call " << c->callee << "(";
      for (size_t i = 0; i < c->args.size(); ++i) {
        if (i) os << ", ";
        os << renderOperandWithAlloc(c->args[i], allocation, frame);
      }
      os << ")";
      break;
    }
    case InstKind::Return: {
      auto* r = static_cast<const ReturnInst*>(inst);
      if (r->hasValue) {
        os << "return " << renderOperandWithAlloc(r->value, allocation, frame);
      } else {
        os << "return";
      }
      break;
    }
    case InstKind::Label: {
      auto* l = static_cast<const LabelInst*>(inst);
      return l->label + ":";
    }
    case InstKind::Load: {
      auto* l = static_cast<const LoadInst*>(inst);
      auto it = allocation.find(l->dest);
      std::string destReg = (it != allocation.end())
                                ? RISCVRegMap::physicalRegName(it->second)
                                : "%v" + std::to_string(l->dest);
      os << "lw " << destReg << ", 0("
         << renderOperandWithAlloc(l->addr, allocation, frame) << ")";
      break;
    }
    case InstKind::Store: {
      auto* s = static_cast<const StoreInst*>(inst);
      os << "sw " << renderOperandWithAlloc(s->src, allocation, frame) << ", 0("
         << renderOperandWithAlloc(s->addr, allocation, frame) << ")";
      break;
    }
  }
  return os.str();
}

void CodeGen::emitFunctionBody(const IRFunction& fn,
                               const std::unordered_map<int, int>& allocation,
                               const StackFrame& frame,
                               const LivenessResult& liveness,
                               std::vector<std::string>& out) {
  (void)liveness;
  const std::vector<std::string> scratchRegs = {"t5", "t6"};

  auto mangleLabel = [&](const std::string& lbl) {
    return fn.name + "_" + lbl;
  };

  auto chooseScratch = [&](const std::vector<std::string>& reservedRegs) {
    for (const auto& reg : scratchRegs) {
      if (std::find(reservedRegs.begin(), reservedRegs.end(), reg) ==
          reservedRegs.end()) {
        return reg;
      }
    }
    return std::string("t6");
  };

  auto loadOperandInto = [&](const Operand& op, const std::string& targetReg,
                             std::vector<std::string>& insns,
                             const std::vector<std::string>& liveRegs =
                                 std::vector<std::string>()) -> std::string {
    auto chooseAddrForTarget = [&](const std::string& valueReg) {
      if (std::find(liveRegs.begin(), liveRegs.end(), valueReg) ==
          liveRegs.end()) {
        return valueReg;
      }
      return chooseScratch(liveRegs);
    };

    if (op.isImm()) {
      emitLoadImmediate(targetReg, op.immValue, insns);
      return targetReg;
    }
    if (op.isVReg()) {
      auto it = allocation.find(op.vregId);
      if (it != allocation.end()) {
        std::string reg = RISCVRegMap::physicalRegName(it->second);
        if (reg != targetReg) {
          insns.push_back("\taddi " + targetReg + ", " + reg + ", 0");
        }
        return targetReg;
      }
      auto valueIt = frame.paramValueSlots.find(op.vregId);
      if (valueIt != frame.paramValueSlots.end()) {
        std::string addrReg = chooseAddrForTarget(targetReg);
        emitStackLoad64(targetReg, valueIt->second, insns, addrReg);
        return targetReg;
      }
      auto paramIndexIt = frame.paramIndexByVReg.find(op.vregId);
      if (paramIndexIt != frame.paramIndexByVReg.end()) {
        int paramIndex = paramIndexIt->second;
        if (paramIndex >= 8) {
          int offset = frame.frameSize + (paramIndex - 8) * 8;
          std::string addrReg = chooseAddrForTarget(targetReg);
          emitStackLoad64(targetReg, offset, insns, addrReg);
          return targetReg;
        }
      }
      auto paramIt = frame.paramSlots.find(op.vregId);
      if (paramIt != frame.paramSlots.end()) {
        std::string addrReg = chooseAddrForTarget(targetReg);
        emitStackLoad64(targetReg, paramIt->second, insns, addrReg);
        return targetReg;
      }
      auto spillIt = frame.spillSlots.find(op.vregId);
      if (spillIt != frame.spillSlots.end()) {
        std::string addrReg = chooseAddrForTarget(targetReg);
        emitStackLoad64(targetReg, spillIt->second, insns, addrReg);
        return targetReg;
      }
      return targetReg;
    }
    if (op.isStackPtr()) {
      insns.push_back("\taddi " + targetReg + ", sp, 0");
      return targetReg;
    }
    if (op.isLocalVarAddr()) {
      // 局部数组地址：sp + localVarOffset + offset
      int totalOffset = frame.localVarOffset + op.immValue;
      emitStackAddress(targetReg, "sp", totalOffset, insns);
      return targetReg;
    }
    emitLoadGlobalAddress(targetReg, op.globalName, insns);
    return targetReg;
  };

  auto loadOperand = [&](const Operand& op, int& scratchIdx,
                         std::vector<std::string>& insns,
                         const std::vector<std::string>& reservedRegs =
                             std::vector<std::string>()) -> std::string {
    (void)scratchIdx;
    std::string reg = chooseScratch(reservedRegs);
    return loadOperandInto(op, reg, insns, reservedRegs);
  };

  auto loadFloatOperand = [&](const Operand& op, const std::string& targetFReg,
                              std::vector<std::string>& insns,
                              const std::vector<std::string>& reservedRegs =
                                  std::vector<std::string>()) -> std::string {
    if (op.isImm() && isFloatValueType(op.valueType)) {
      emitLoadFloatImmediate(targetFReg, op.immFloatValue, insns);
      return targetFReg;
    }
    std::string intReg = chooseScratch(reservedRegs);
    loadOperandInto(op, intReg, insns, reservedRegs);
    insns.push_back("\tfmv.w.x " + targetFReg + ", " + intReg);
    return targetFReg;
  };

  auto storeWithScratch = [&](const std::string& valueReg, int offset,
                              std::vector<std::string>& insns,
                              const std::vector<std::string>& liveRegs =
                                  std::vector<std::string>()) {
    std::vector<std::string> reserved = liveRegs;
    reserved.push_back(valueReg);
    emitStackStore(valueReg, offset, insns, chooseScratch(reserved));
  };

  auto storeWithScratch64 = [&](const std::string& valueReg, int offset,
                                std::vector<std::string>& insns,
                                const std::vector<std::string>& liveRegs =
                                    std::vector<std::string>()) {
    std::vector<std::string> reserved = liveRegs;
    reserved.push_back(valueReg);
    emitStackStore64(valueReg, offset, insns, chooseScratch(reserved));
  };

  auto storeCurrentValue = [&](int vreg, const std::string& valueReg,
                               std::vector<std::string>& insns,
                               const std::vector<std::string>& liveRegs =
                                   std::vector<std::string>()) {
    auto valueIt = frame.paramValueSlots.find(vreg);
    if (valueIt != frame.paramValueSlots.end()) {
      storeWithScratch64(valueReg, valueIt->second, insns, liveRegs);
      return;
    }
    auto spillIt = frame.spillSlots.find(vreg);
    if (spillIt != frame.spillSlots.end()) {
      storeWithScratch64(valueReg, spillIt->second, insns, liveRegs);
    }
  };

  auto storeIncomingParam = [&](int vreg, const std::string& valueReg,
                                std::vector<std::string>& insns,
                                const std::vector<std::string>& liveRegs =
                                    std::vector<std::string>()) {
    auto paramIt = frame.paramSlots.find(vreg);
    if (paramIt != frame.paramSlots.end()) {
      storeWithScratch64(valueReg, paramIt->second, insns, liveRegs);
    }
    storeCurrentValue(vreg, valueReg, insns, liveRegs);
  };

  auto storeFloatResult = [&](int vreg, const std::string& srcFReg,
                              std::vector<std::string>& insns) {
    auto it = allocation.find(vreg);
    if (it != allocation.end()) {
      std::string dst = RISCVRegMap::physicalRegName(it->second);
      insns.push_back("\tfmv.x.w " + dst + ", " + srcFReg);
    } else {
      std::string tmp = chooseScratch({});
      insns.push_back("\tfmv.x.w " + tmp + ", " + srcFReg);
      storeCurrentValue(vreg, tmp, insns, {tmp});
    }
  };

  auto emitFloatCompare = [&](BinaryOp op, const std::string& destReg,
                              const std::string& lhsF, const std::string& rhsF,
                              std::vector<std::string>& insns) {
    switch (op) {
      case BinaryOp::Lt:
        insns.push_back("\tflt.s " + destReg + ", " + lhsF + ", " + rhsF);
        break;
      case BinaryOp::Gt:
        insns.push_back("\tflt.s " + destReg + ", " + rhsF + ", " + lhsF);
        break;
      case BinaryOp::Le:
        insns.push_back("\tfle.s " + destReg + ", " + lhsF + ", " + rhsF);
        break;
      case BinaryOp::Ge:
        insns.push_back("\tfle.s " + destReg + ", " + rhsF + ", " + lhsF);
        break;
      case BinaryOp::Eq:
        insns.push_back("\tfeq.s " + destReg + ", " + lhsF + ", " + rhsF);
        break;
      case BinaryOp::Ne:
        insns.push_back("\tfeq.s " + destReg + ", " + lhsF + ", " + rhsF);
        insns.push_back("\txori " + destReg + ", " + destReg + ", 1");
        break;
      default:
        break;
    }
  };

  // Move incoming parameters to their assigned locations.
  int incomingIntRegCount = 0;
  int incomingFloatRegCount = 0;
  int incomingStackCount = 0;
  for (size_t i = 0; i < fn.params.size(); ++i) {
    int vreg = fn.params[i];
    int scratchIdx = 0;
    std::vector<std::string> insns;
    ValueType paramType = i < fn.paramTypes.size() ? fn.paramTypes[i] : ValueType::I32;
    ArgLocation loc = classifyArgLocation(paramType, incomingIntRegCount,
                                          incomingFloatRegCount, incomingStackCount);

    if (loc.useFloatReg) {
      auto it = allocation.find(vreg);
      if (it != allocation.end()) {
        std::string dst = RISCVRegMap::physicalRegName(it->second);
        insns.push_back("\tfmv.x.w " + dst + ", fa" + std::to_string(loc.regIndex));
      } else {
        std::string tmp = chooseScratch({});
        insns.push_back("\tfmv.x.w " + tmp + ", fa" + std::to_string(loc.regIndex));
        storeIncomingParam(vreg, tmp, insns, {tmp});
      }
    } else {
      std::string srcReg;
      if (loc.useIntReg) {
        srcReg = "a" + std::to_string(loc.regIndex);
      } else {
        int offset = frame.frameSize + loc.stackIndex * 8;
        srcReg = chooseScratch({});
        emitStackLoad64(srcReg, offset, insns, srcReg);
      }

      auto it = allocation.find(vreg);
      if (it != allocation.end()) {
        std::string dst = RISCVRegMap::physicalRegName(it->second);
        if (dst != srcReg) {
          if (paramType == ValueType::I32) {
            insns.push_back("\taddiw " + dst + ", " + srcReg + ", 0");
          } else {
            insns.push_back("\taddi " + dst + ", " + srcReg + ", 0");
          }
        }
      } else {
        storeIncomingParam(vreg, srcReg, insns, {srcReg});
      }
    }

    for (const auto& line : insns) {
      out.push_back(line);
    }
  }

  // Emit instructions
  for (size_t idx = 0; idx < fn.instructions.size(); ++idx) {
    const auto& inst = fn.instructions[idx];
    int scratchIdx = 0;
    std::vector<std::string> insns;

    switch (inst->kind) {
      case InstKind::Binary: {
        auto* b = static_cast<const BinaryInst*>(inst.get());
        if (isFloatValueType(b->operandType)) {
          std::string lhsF = loadFloatOperand(b->lhs, "ft0", insns);
          std::string rhsF = loadFloatOperand(b->rhs, "ft1", insns, {"t6"});
          if (isFloatValueType(b->resultType)) {
            switch (b->op) {
              case BinaryOp::Add:
                insns.push_back("\tfadd.s ft2, " + lhsF + ", " + rhsF);
                break;
              case BinaryOp::Sub:
                insns.push_back("\tfsub.s ft2, " + lhsF + ", " + rhsF);
                break;
              case BinaryOp::Mul:
                insns.push_back("\tfmul.s ft2, " + lhsF + ", " + rhsF);
                break;
              case BinaryOp::Div:
                insns.push_back("\tfdiv.s ft2, " + lhsF + ", " + rhsF);
                break;
              default:
                break;
            }
            storeFloatResult(b->dest, "ft2", insns);
          } else {
            auto it = allocation.find(b->dest);
            std::string destReg =
                it != allocation.end() ? RISCVRegMap::physicalRegName(it->second)
                                       : chooseScratch({});
            emitFloatCompare(b->op, destReg, lhsF, rhsF, insns);
            storeCurrentValue(b->dest, destReg, insns, {destReg});
          }
          break;
        }

        std::string lhs = loadOperand(b->lhs, scratchIdx, insns);
        std::string rhs = loadOperand(b->rhs, scratchIdx, insns, {lhs});

        auto it = allocation.find(b->dest);
        std::string destReg;
        if (it != allocation.end()) {
          destReg = RISCVRegMap::physicalRegName(it->second);
        } else {
          std::vector<std::string> reservedRegs{lhs, rhs};
          destReg = chooseScratch(reservedRegs);
        }

        switch (b->op) {
          case BinaryOp::Eq:
            insns.push_back("\txor " + destReg + ", " + lhs + ", " + rhs);
            insns.push_back("\tsltiu " + destReg + ", " + destReg + ", 1");
            break;
          case BinaryOp::Ne:
            insns.push_back("\txor " + destReg + ", " + lhs + ", " + rhs);
            insns.push_back("\tsltu " + destReg + ", x0, " + destReg);
            break;
          case BinaryOp::Gt:
            insns.push_back("\tslt " + destReg + ", " + rhs + ", " + lhs);
            break;
          case BinaryOp::Le:
            insns.push_back("\tslt " + destReg + ", " + rhs + ", " + lhs);
            insns.push_back("\txori " + destReg + ", " + destReg + ", 1");
            break;
          case BinaryOp::Ge:
            insns.push_back("\tslt " + destReg + ", " + lhs + ", " + rhs);
            insns.push_back("\txori " + destReg + ", " + destReg + ", 1");
            break;
          case BinaryOp::Add:
            if (b->rhs.isImm() && isInt12(b->rhs.immValue)) {
              std::string addiOp =
                  b->operandType == ValueType::I32 ? "addiw" : "addi";
              insns.push_back("\t" + addiOp + " " + destReg + ", " + lhs +
                              ", " + std::to_string(b->rhs.immValue));
            } else if (b->lhs.isImm() && isInt12(b->lhs.immValue)) {
              std::string addiOp =
                  b->operandType == ValueType::I32 ? "addiw" : "addi";
              insns.push_back("\t" + addiOp + " " + destReg + ", " + rhs +
                              ", " + std::to_string(b->lhs.immValue));
            } else {
              std::string addOp =
                  b->operandType == ValueType::I32 ? "addw" : "add";
              insns.push_back("\t" + addOp + " " + destReg + ", " + lhs +
                              ", " + rhs);
            }
            break;
          case BinaryOp::Mul:
            if (b->rhs.isImm() && b->rhs.immValue > 0 &&
                (b->rhs.immValue & (b->rhs.immValue - 1)) == 0) {
              std::string shiftOp = b->operandType == ValueType::I32 ? "slliw" : "slli";
              insns.push_back("\t" + shiftOp + " " + destReg + ", " + lhs + ", " +
                              std::to_string(__builtin_ctz(b->rhs.immValue)));
            } else if (b->lhs.isImm() && b->lhs.immValue > 0 &&
                       (b->lhs.immValue & (b->lhs.immValue - 1)) == 0) {
              std::string shiftOp = b->operandType == ValueType::I32 ? "slliw" : "slli";
              insns.push_back("\t" + shiftOp + " " + destReg + ", " + rhs + ", " +
                              std::to_string(__builtin_ctz(b->lhs.immValue)));
            } else {
              insns.push_back("\t" + binOpMnemonic(b->op, b->operandType) + " " + destReg + ", " +
                              lhs + ", " + rhs);
            }
            break;
          default:
            insns.push_back("\t" + binOpMnemonic(b->op, b->operandType) + " " + destReg + ", " +
                            lhs + ", " + rhs);
            break;
        }
        storeCurrentValue(b->dest, destReg, insns);
        break;
      }
      case InstKind::Unary: {
        auto* u = static_cast<const UnaryInst*>(inst.get());
        if (isFloatValueType(u->operandType)) {
          if (u->op == UnaryOp::Plus) {
            std::string src = loadOperand(u->operand, scratchIdx, insns);
            auto it = allocation.find(u->dest);
            if (it != allocation.end()) {
              std::string dst = RISCVRegMap::physicalRegName(it->second);
              if (dst != src) {
                insns.push_back("\taddi " + dst + ", " + src + ", 0");
              }
            } else {
              storeCurrentValue(u->dest, src, insns, {src});
            }
            break;
          }

          std::string opndF = loadFloatOperand(u->operand, "ft0", insns);
          if (u->op == UnaryOp::Neg) {
            insns.push_back("\tfneg.s ft1, " + opndF);
            storeFloatResult(u->dest, "ft1", insns);
          } else {
            insns.push_back("\tfmv.w.x ft1, x0");
            auto it = allocation.find(u->dest);
            std::string destReg =
                it != allocation.end() ? RISCVRegMap::physicalRegName(it->second)
                                       : chooseScratch({});
            insns.push_back("\tfeq.s " + destReg + ", " + opndF + ", ft1");
            storeCurrentValue(u->dest, destReg, insns, {destReg});
          }
          break;
        }

        std::string opnd = loadOperand(u->operand, scratchIdx, insns);
        auto it = allocation.find(u->dest);
        std::string destReg;
        if (it != allocation.end()) {
          destReg = RISCVRegMap::physicalRegName(it->second);
        } else {
          destReg = chooseScratch({opnd});
        }

        switch (u->op) {
          case UnaryOp::Neg:
            if (u->operandType == ValueType::I32) {
              insns.push_back("\tsubw " + destReg + ", x0, " + opnd);
            } else {
              insns.push_back("\tsub " + destReg + ", x0, " + opnd);
            }
            break;
          case UnaryOp::Not:
            insns.push_back("\tsltiu " + destReg + ", " + opnd + ", 1");
            break;
          case UnaryOp::Plus:
            if (u->operandType == ValueType::I32) {
              insns.push_back("\taddiw " + destReg + ", " + opnd + ", 0");
            } else {
              insns.push_back("\taddi " + destReg + ", " + opnd + ", 0");
            }
            break;
        }
        storeCurrentValue(u->dest, destReg, insns);
        break;
      }
      case InstKind::Copy: {
        auto* c = static_cast<const CopyInst*>(inst.get());
        if (isFloatValueType(c->destType) && !isFloatValueType(c->src.valueType)) {
          std::string src = loadOperand(c->src, scratchIdx, insns);
          insns.push_back("\tfcvt.s.w ft0, " + src);
          storeFloatResult(c->dest, "ft0", insns);
          break;
        }
        if (!isFloatValueType(c->destType) && isFloatValueType(c->src.valueType)) {
          std::string srcF = loadFloatOperand(c->src, "ft0", insns);
          auto it = allocation.find(c->dest);
          std::string dst =
              it != allocation.end() ? RISCVRegMap::physicalRegName(it->second)
                                     : chooseScratch({});
          insns.push_back("\tfcvt.w.s " + dst + ", " + srcF + ", rtz");
          storeCurrentValue(c->dest, dst, insns, {dst});
          break;
        }

        std::string src = loadOperand(c->src, scratchIdx, insns);
        auto it = allocation.find(c->dest);
        if (it != allocation.end()) {
          std::string dst = RISCVRegMap::physicalRegName(it->second);
          if (c->destType == ValueType::I32) {
            insns.push_back("\taddiw " + dst + ", " + src + ", 0");
          } else {
            insns.push_back("\taddi " + dst + ", " + src + ", 0");
          }
        } else {
          storeCurrentValue(c->dest, src, insns, {src});
        }
        break;
      }
      case InstKind::Load: {
        auto* l = static_cast<const LoadInst*>(inst.get());
        std::string addr = loadOperand(l->addr, scratchIdx, insns);
        auto it = allocation.find(l->dest);
        std::string dst;
        if (it != allocation.end()) {
          dst = RISCVRegMap::physicalRegName(it->second);
        } else {
          dst = chooseScratch({addr});
        }
        insns.push_back("\tlw " + dst + ", 0(" + addr + ")");
        storeCurrentValue(l->dest, dst, insns);
        break;
      }
      case InstKind::Store: {
        auto* s = static_cast<const StoreInst*>(inst.get());
        std::string src = loadOperand(s->src, scratchIdx, insns);
        std::string addr = loadOperand(s->addr, scratchIdx, insns, {src});
        insns.push_back("\tsw " + src + ", 0(" + addr + ")");
        break;
      }
      case InstKind::Branch: {
        auto* br = static_cast<const BranchInst*>(inst.get());
        if (isFloatValueType(br->cond.valueType)) {
          std::string condF = loadFloatOperand(br->cond, "ft0", insns);
          insns.push_back("\tfmv.w.x ft1, x0");
          insns.push_back("\tfeq.s t0, " + condF + ", ft1");
          insns.push_back("\tbeq t0, x0, " + mangleLabel(br->trueLabel));
        } else {
          std::string cond = loadOperand(br->cond, scratchIdx, insns);
          insns.push_back("\tbne " + cond + ", x0, " +
                          mangleLabel(br->trueLabel));
        }
        insns.push_back("\tjal x0, " + mangleLabel(br->falseLabel));
        break;
      }
      case InstKind::Jump: {
        auto* j = static_cast<const JumpInst*>(inst.get());
        insns.push_back("\tjal x0, " + mangleLabel(j->target));
        break;
      }
      case InstKind::Call: {
        auto* c = static_cast<const CallInst*>(inst.get());

        std::string destRegName;
        if (c->hasDest) {
          auto it = allocation.find(c->dest);
          if (it != allocation.end()) {
            destRegName = RISCVRegMap::physicalRegName(it->second);
          }
        }

        for (const auto& reg : frame.callerSavedRegs) {
          auto itSlot = frame.callerSavedSlots.find(reg);
          if (itSlot != frame.callerSavedSlots.end()) {
            storeWithScratch64(reg, itSlot->second, insns);
          }
        }

        int outgoingIntRegCount = 0;
        int outgoingFloatRegCount = 0;
        int outgoingStackCount = 0;
        for (size_t ai = 0; ai < c->args.size(); ++ai) {
          ValueType argType = ai < c->argTypes.size() ? c->argTypes[ai] : ValueType::I32;
          ArgLocation loc = classifyArgLocation(argType, outgoingIntRegCount,
                                                outgoingFloatRegCount, outgoingStackCount);
          if (loc.useFloatReg) {
            loadFloatOperand(c->args[ai], "fa" + std::to_string(loc.regIndex), insns);
            continue;
          }

          std::vector<std::string> reservedRegs;
          if (loc.useIntReg) reservedRegs.push_back("a" + std::to_string(loc.regIndex));
          std::string argReg =
              loadOperand(c->args[ai], scratchIdx, insns, reservedRegs);
          if (loc.useIntReg) {
            std::string target = "a" + std::to_string(loc.regIndex);
            if (argReg != target) {
              if (argType == ValueType::I32) {
                insns.push_back("\taddiw " + target + ", " + argReg + ", 0");
              } else {
                insns.push_back("\taddi " + target + ", " + argReg + ", 0");
              }
            }
          } else {
            int offset = frame.outgoingArgOffset + loc.stackIndex * 8;
            storeWithScratch64(argReg, offset, insns);
          }
        }

        insns.push_back("\tcall " + c->callee);

        for (const auto& reg : frame.callerSavedRegs) {
          if (!destRegName.empty() && reg == destRegName) {
            continue;
          }
          auto itSlot = frame.callerSavedSlots.find(reg);
          if (itSlot != frame.callerSavedSlots.end()) {
            emitStackLoad64(reg, itSlot->second, insns, reg);
          }
        }

        if (c->hasDest) {
          if (isFloatValueType(c->resultType)) {
            auto it = allocation.find(c->dest);
            if (it != allocation.end()) {
              std::string dst = RISCVRegMap::physicalRegName(it->second);
              insns.push_back("\tfmv.x.w " + dst + ", fa0");
            } else {
              std::string tmp = chooseScratch({});
              insns.push_back("\tfmv.x.w " + tmp + ", fa0");
              storeCurrentValue(c->dest, tmp, insns, {tmp});
            }
          } else {
            auto it = allocation.find(c->dest);
            if (it != allocation.end()) {
              std::string dst = RISCVRegMap::physicalRegName(it->second);
              if (c->resultType == ValueType::I32) {
                insns.push_back("\taddiw " + dst + ", a0, 0");
              } else {
                insns.push_back("\taddi " + dst + ", a0, 0");
              }
            } else {
              storeCurrentValue(c->dest, "a0", insns, {"a0"});
            }
          }
        }

        break;
      }
      case InstKind::Return: {
        auto* r = static_cast<const ReturnInst*>(inst.get());
        if (r->hasValue) {
          if (isFloatValueType(r->valueType)) {
            loadFloatOperand(r->value, "fa0", insns);
          } else {
            std::string valReg = loadOperand(r->value, scratchIdx, insns);
            if (valReg != "a0") {
              if (r->valueType == ValueType::I32) {
                insns.push_back("\taddiw a0, " + valReg + ", 0");
              } else {
                insns.push_back("\taddi a0, " + valReg + ", 0");
              }
            }
          }
        }
        for (const auto& line : insns) {
          out.push_back(line);
        }
        emitEpilogue(fn, frame, out);
        continue;
      }
      case InstKind::Label: {
        auto* l = static_cast<const LabelInst*>(inst.get());
        out.push_back(mangleLabel(l->label) + ":");
        continue;
      }
    }

    for (const auto& line : insns) {
      out.push_back(line);
    }
  }

  bool endsWithReturn = false;
  if (!fn.instructions.empty()) {
    const auto& lastInst = fn.instructions.back();
    endsWithReturn = (lastInst->kind == InstKind::Return);
  }
  if (!endsWithReturn) {
    emitEpilogue(fn, frame, out);
  }
}

std::vector<std::string> CodeGen::generate(const IRProgram& program) {
  std::vector<std::string> out;

  if (!program.globals.empty() || !program.globalArrays.empty()) {
    out.push_back(".data");
    out.push_back("");
    for (const auto& glob : program.globals) {
      out.push_back(".globl " + glob.name);
      out.push_back(glob.name + ":");
      if (glob.valueType == ValueType::F32) {
        out.push_back("\t.word " + std::to_string(floatBits(glob.typedInitialValue.floatValue)));
      } else {
        out.push_back("\t.word " + std::to_string(glob.initialValue));
      }
      out.push_back("");
    }
    // 输出全局数组
    for (const auto& arr : program.globalArrays) {
      out.push_back(".globl " + arr.name);
      out.push_back(arr.name + ":");
      int totalSize = 1;
      for (int dim : arr.dimensions) {
        totalSize *= dim;
      }
      if (arr.initialValues.empty()) {
        // 零初始化
        out.push_back("\t.zero " + std::to_string(totalSize * 4));
      } else {
        // 逐元素输出
        for (const auto& val : arr.initialValues) {
          if (arr.elementType == ValueType::F32) {
            out.push_back("\t.word " + std::to_string(floatBits(val.floatValue)));
          } else {
            out.push_back("\t.word " + std::to_string(val.intValue));
          }
        }
        // 如果初始化值不够，补零
        for (size_t i = arr.initialValues.size(); i < static_cast<size_t>(totalSize); ++i) {
          out.push_back("\t.word 0");
        }
      }
      out.push_back("");
    }
  }

  out.push_back(".text");
  out.push_back("");

  GraphColoringAllocator allocator;

  for (auto& fn : program.functions) {
    LivenessResult liveness = AnalyzeLiveness(fn);
    RegAllocResult allocResult = allocator.allocate(fn, liveness);
    StackFrame frame = computeStackFrame(fn, allocResult, liveness);

    emitPrologue(fn, frame, out);
    emitFunctionBody(fn, allocResult.allocation, frame, liveness, out);

    out.push_back("");
  }

  // Debug hook: keep raw assembly when SYSC_RAW_ASM is set.
  if (std::getenv("SYSC_RAW_ASM") == nullptr) {
    out = peepholeOptimize(out);
  }

  return out;
}
