import os
import sys
import re

i=0
AnalyzedBasicBlocks = 0
FunctionsAndBlocksAnalyzed = 0
FunctionsAsToplevel = 0
PercentReachableBasicBlocks = 0
MaximumBBsInFunction = 0
NumPathsAnalyzed = 0
NumXTUCalled = 0
NumXTUSuccess = 0
NumStepsExecuted = 0
NumStepsExecuted50P = 0
NumStepsExecutedMax = 0
NumStepsExecutedMin = 0
NumStepsExecutedMaxTemp= 0
NumStepsExecutedMinTemp = 0
AbortedPathsMaxBlock = 0
AbortedPathsMaxBlockInline = 0
DeadBindingsCalled = 0
NumCallInlined = 0
NumCallReeval = 0
InlineCountMax = 0
NumReachedMaxSteps = 0
LastAnalyzedBasicBlocks = 0
AllBBs = 0


NumXTUCalledPat = re.compile("(\d+) ASTContext       - The # of getXTUDefinition function called")
NumXTUSuccessPat = re.compile("(\d+) ASTContext       - The # of getXTUDefinition successfully return the requested function's body")
NumReachedMaxStepsPat = re.compile("(\d+) CoreEngine       - The # of times we reached the max number of steps.")
InlineCountMaxPat = re.compile("(\d+) ExprEngine       - The # of times we reached inline count maximum")
NumStepsExecutedMaxPat = re.compile("(\d+) CoreEngine       - The max # of steps in a path.")
NumStepsExecutedMinPat = re.compile("(\d+) CoreEngine       - The min # of steps in a path.")
AnalyzedBasicBlocksPat = re.compile("(\d+) AnalysisConsumer - The # of basic blocks in the analyzed functions")
FunctionsAndBlocksAnalyzedPat = re.compile("(\d+) AnalysisConsumer - The # of functions and blocks analyzed \(as top level with inlining turned on\)")
FunctionsAsToplevelPat = re.compile("(\d+) AnalysisConsumer - The # of functions at top level")
PercentReachableBasicBlocksPat = re.compile("(\d+) AnalysisConsumer - The % of reachable basic blocks")
MaximumBBsInFunctionPat = re.compile("(\d+) AnalysisConsumer - The maximum number of basic blocks in a function")
NumPathsAnalyzedPat = re.compile("(\d+) CoreEngine       - The # of paths explored by the analyzer")
NumStepsExecutedPat = re.compile("(\d+) CoreEngine       - The # of steps executed")
AbortedPathsMaxBlockPat = re.compile("(\d+) ExprEngine       - The # of aborted paths due to reaching the maximum block count in a top level function")
AbortedPathsMaxBlockInlinePat = re.compile("(\d+) ExprEngine       - The # of aborted paths due to reaching the maximum block count in an inlined function")
  
DeadBindingsCalledPat = re.compile("(\d+) ExprEngine       - The # of times RemoveDeadBindings is called")
NumCallInlinedPat = re.compile("(\d+) ExprEngine       - The # of times we inlined a call")
NumCallReevalPat = re.compile("(\d+) ExprEngine       - The # of times we re-evaluated a call without inlining")

with open(sys.argv[1]) as f:
    content = f.readlines()
    for line in content:
        m = NumXTUCalledPat.search(line)
        if m:
            NumXTUCalled += int(m.group(1))

        m = NumXTUSuccessPat.search(line)
        if m:
            NumXTUSuccess += int(m.group(1))

        m = InlineCountMaxPat.search(line)
        if m:
            InlineCountMax += int(m.group(1))

        m = NumReachedMaxStepsPat.search(line)
        if m:
            NumReachedMaxSteps += int(m.group(1))

        m = AnalyzedBasicBlocksPat.search(line)
        if m:
            AnalyzedBasicBlocks += int(m.group(1))
            LastAnalyzedBasicBlocks = int(m.group(1))
            
        m = FunctionsAndBlocksAnalyzedPat.search(line)
        if m:
            FunctionsAndBlocksAnalyzed += int(m.group(1))

        m = FunctionsAsToplevelPat.search(line)
        if m:
            FunctionsAsToplevel += int(m.group(1))
            
        m = PercentReachableBasicBlocksPat.search(line)
        if m:
            AnalyzedPercent = int(m.group(1))
            if AnalyzedPercent > 0:
                AllBBs += LastAnalyzedBasicBlocks * 100 / AnalyzedPercent
            
        m = MaximumBBsInFunctionPat.search(line)
        if m:
            MaximumBBsInFunction = max(int(m.group(1)), MaximumBBsInFunction)
            
        m = NumPathsAnalyzedPat.search(line)
        if m:
            NumPathsAnalyzed += int(m.group(1))
            
        m = NumStepsExecutedMaxPat.search(line)
        if m:
            NumStepsExecutedMaxTemp = int(m.group(1))
        
        m = NumStepsExecutedMinPat.search(line)
        if m:
            NumStepsExecutedMinTemp = int(m.group(1))
        
        m = NumStepsExecutedPat.search(line)
        if m:
            i+=1
            NumStepsExecuted += int(m.group(1))
            #NumStepsExecutedMax += NumStepsExecutedMaxTemp/float(m.group(1))
            #NumStepsExecutedMin += NumStepsExecutedMinTemp/float(m.group(1))
            #if NumStepsExecutedMaxTemp/float(m.group(1)) >= 0.5:
            #    NumStepsExecuted50P += 1


        m = AbortedPathsMaxBlockPat.search(line)
        if m:
            AbortedPathsMaxBlock += int(m.group(1))
            
        m = AbortedPathsMaxBlockInlinePat.search(line)
        if m:
            AbortedPathsMaxBlockInline += int(m.group(1))
        
        m = DeadBindingsCalledPat.search(line)
        if m:
            DeadBindingsCalled += int(m.group(1))
            
        m = NumCallInlinedPat.search(line)
        if m:
            NumCallInlined += int(m.group(1))
            
        m = NumCallReevalPat.search(line)
        if m:
            NumCallReeval += int(m.group(1))


print str(NumXTUCalled) + "-The # of getXTUDefinition function called"
print str(NumXTUSuccess) + "-The # of getXTUDefinition successfully return the requested function's body"
print str(AnalyzedBasicBlocks) + "-The # of basic blocks in the analyzed functions."
print str(FunctionsAndBlocksAnalyzed) + "-The # of functions and blocks analyzed (as top level with inlining turned on)."
print str(FunctionsAsToplevel) + "-The # of functions at top level."
if AllBBs>0:
    print str(AnalyzedBasicBlocks*100/float(AllBBs)) + "-The % of reachable basic blocks."
print str(MaximumBBsInFunction) + "-The maximum number of basic blocks in a function."
print str(NumPathsAnalyzed) + "-The # of paths explored by the analyzer."
print str(NumStepsExecuted) + "-The # of steps executed."
print str(NumReachedMaxSteps) + "-The # of times we reached the max number of steps."
print str(AbortedPathsMaxBlock) + "-The # of aborted paths due to reaching the maximum block count in a top level function"
print str(AbortedPathsMaxBlockInline) + "-The # of aborted paths due to reaching the maximum block count in an inlined function"
print str(DeadBindingsCalled) + "-The # of times RemoveDeadBindings is called"
print str(NumCallInlined) + "-The # of times we inlined a call"
print str(InlineCountMax) + "-The # of times we reached inline count maximum"
print str(NumCallReeval) + "-The # of times we reevaluated a call without inlining"

