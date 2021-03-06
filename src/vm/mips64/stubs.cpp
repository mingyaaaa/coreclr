// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// Copyright (c) Loongson Technology. All rights reserved.

//
// File: stubs.cpp
//
// This file contains stub functions for unimplemented features need to
// run on the MIPS64 platform.

#include "common.h"
#include "dllimportcallback.h"
#include "comdelegate.h"
#include "asmconstants.h"
#include "virtualcallstub.h"
#include "jitinterface.h"
#include "ecall.h"

EXTERN_C void JIT_UpdateWriteBarrierState(bool skipEphemeralCheck);


#ifndef DACCESS_COMPILE
//-----------------------------------------------------------------------
// InstructionFormat for B.cond
//-----------------------------------------------------------------------
class ConditionalBranchInstructionFormat : public InstructionFormat
{

    public:
        ConditionalBranchInstructionFormat() : InstructionFormat(InstructionFormat::k32)
        {
            LIMITED_METHOD_CONTRACT;
        }

        virtual UINT GetSizeOfInstruction(UINT refsize, UINT variationCode)
        {
            LIMITED_METHOD_CONTRACT;

            _ASSERTE(refsize == InstructionFormat::k32);

            return 4;
        }

        virtual UINT GetHotSpotOffset(UINT refsize, UINT variationCode)
        {
            WRAPPER_NO_CONTRACT;
            return 0;
        }


        virtual BOOL CanReach(UINT refSize, UINT variationCode, BOOL fExternal, INT_PTR offset)
        {
            _ASSERTE(!fExternal || "MIPS64:NYI - CompareAndBranchInstructionFormat::CanReach external");
            if (fExternal)
                return false;

            if (offset < -1048576 || offset > 1048572)
                return false;
            return true;
        }
        // B.<cond> <label>
        // Encoding 0|1|0|1|0|1|0|0|imm19|0|cond
        // cond = Bits3-0(variation)
        // imm19 = bits19-0(fixedUpReference/4), will be SignExtended
        virtual VOID EmitInstruction(UINT refSize, __int64 fixedUpReference, BYTE *pOutBuffer, UINT variationCode, BYTE *pDataBuffer, BYTE adjustOffset = 0)
        {
            _ASSERTE(!"MIPS64: not implementation on mips64!!!");
            LIMITED_METHOD_CONTRACT;

            _ASSERTE(refSize == InstructionFormat::k32);

            if (fixedUpReference < -1048576 || fixedUpReference > 1048572)
                COMPlusThrow(kNotSupportedException);

            _ASSERTE((fixedUpReference & 0x3) == 0);
            DWORD imm19 = (DWORD)(0x7FFFF & (fixedUpReference >> 2));

            pOutBuffer[0] = static_cast<BYTE>((0x7 & imm19 /* Bits2-0(imm19) */) << 5  | (0xF & variationCode /* cond */));
            pOutBuffer[1] = static_cast<BYTE>((0x7F8 & imm19 /* Bits10-3(imm19) */) >> 3);
            pOutBuffer[2] = static_cast<BYTE>((0x7F800 & imm19 /* Bits19-11(imm19) */) >> 11);
            pOutBuffer[3] = static_cast<BYTE>(0x54);
        }
};

//-----------------------------------------------------------------------
// InstructionFormat for J(AL)R (unconditional branch)
//-----------------------------------------------------------------------
class BranchInstructionFormat : public InstructionFormat
{
    // Encoding of the VariationCode:
    // bit(0) indicates whether this is a direct or an indirect jump.
    // bit(1) indicates whether this is a branch with link -a.k.a call- (BL(R)) or not (B(R))

    public:
        enum VariationCodes
        {
            BIF_VAR_INDIRECT           = 0x00000001,
            BIF_VAR_CALL               = 0x00000002,

            BIF_VAR_JUMP               = 0x00000000,
            BIF_VAR_INDIRECT_CALL      = 0x00000003
        };
    private:
        BOOL IsIndirect(UINT variationCode)
        {
            return (variationCode & BIF_VAR_INDIRECT) != 0;
        }
        BOOL IsCall(UINT variationCode)
        {
            return (variationCode & BIF_VAR_CALL) != 0;
        }


    public:
        BranchInstructionFormat() : InstructionFormat(InstructionFormat::k64)
        {
            LIMITED_METHOD_CONTRACT;
        }

        virtual UINT GetSizeOfInstruction(UINT refSize, UINT variationCode)
        {
            LIMITED_METHOD_CONTRACT;
            _ASSERTE(refSize == InstructionFormat::k64);

            if (IsIndirect(variationCode))
                return 16;
            else
                return 12;
        }

        virtual UINT GetSizeOfData(UINT refSize, UINT variationCode)
        {
            WRAPPER_NO_CONTRACT;
            return 8;
        }

        virtual UINT GetHotSpotOffset(UINT refsize, UINT variationCode)
        {
            WRAPPER_NO_CONTRACT;
            return 0;
        }

        virtual BOOL CanReach(UINT refSize, UINT variationCode, BOOL fExternal, INT_PTR offset)
        {
            if (fExternal)
            {
                // Note that the parameter 'offset' is not an offset but the target address itself (when fExternal is true)
                return (refSize == InstructionFormat::k64);
            }
            else
            {
                return ((offset >= -32768 && offset <= 32767) || (refSize == InstructionFormat::k64));
            }
        }

        virtual VOID EmitInstruction(UINT refSize, __int64 fixedUpReference, BYTE *pOutBuffer, UINT variationCode, BYTE *pDataBuffer, BYTE adjustOffset = 0)
        {
            LIMITED_METHOD_CONTRACT;

            if (IsIndirect(variationCode))
            {
                _ASSERTE(((UINT_PTR)pDataBuffer & 7) == 0);

                __int64 dataOffset = pDataBuffer + adjustOffset - pOutBuffer;

                if (dataOffset < -32768 || dataOffset > 32767)
                    COMPlusThrow(kNotSupportedException);

                UINT16 imm16 = (UINT16)(0xFFFF & dataOffset);
                //ld  t9,dataOffset(t9)
                //ld  t9,0(t9)
                //j(al)r  t9
                //nop

                *(DWORD*)pOutBuffer = 0xdf390000 | imm16;
                *(DWORD*)(pOutBuffer + 4) = 0xdf390000;
                if (IsCall(variationCode))
                {
                    *(DWORD*)(pOutBuffer+8) = 0x0320f809; // jalr t9
                }
                else
                {
                    *(DWORD*)(pOutBuffer+8) = 0x03200008; // jr t9
                }
                *(DWORD*)(pOutBuffer+12) = 0;

                *((__int64*)pDataBuffer) = fixedUpReference + (__int64)pOutBuffer;
            }
            else
            {
                _ASSERTE(((UINT_PTR)pDataBuffer & 7) == 0);

                __int64 dataOffset = pDataBuffer + adjustOffset - pOutBuffer;

                if (dataOffset < -32768 || dataOffset > 32767)
                    COMPlusThrow(kNotSupportedException);

                UINT16 imm16 = (UINT16)(0xFFFF & dataOffset);
                //ld  t9,dataOffset(t9)
                //j(al)r  t9
                //nop

                *(DWORD*)pOutBuffer = 0xdf390000 | imm16;
                if (IsCall(variationCode))
                {
                    *(DWORD*)(pOutBuffer+4) = 0x0320f809; // jalr t9
                }
                else
                {
                    *(DWORD*)(pOutBuffer+4) = 0x03200008; // jr t9
                }
                *((DWORD*)(pOutBuffer+8)) = 0;

                if (!ClrSafeInt<__int64>::addition(fixedUpReference, (__int64)pOutBuffer, fixedUpReference))
                    COMPlusThrowArithmetic();
                *((__int64*)pDataBuffer) = fixedUpReference;
            }
        }

};

//-----------------------------------------------------------------------
// InstructionFormat for loading a label to the register (ADRP/ADR)
//-----------------------------------------------------------------------
class LoadFromLabelInstructionFormat : public InstructionFormat
{
/* FIXME for MIPS: should re-design.*/
    public:
        LoadFromLabelInstructionFormat() : InstructionFormat( InstructionFormat::k32)
        {
            LIMITED_METHOD_CONTRACT;
        }

        virtual UINT GetSizeOfInstruction(UINT refSize, UINT variationCode)
        {
            WRAPPER_NO_CONTRACT;
            return 8;

        }

        virtual UINT GetHotSpotOffset(UINT refsize, UINT variationCode)
        {
            WRAPPER_NO_CONTRACT;
            return 0;
        }

        virtual BOOL CanReach(UINT refSize, UINT variationCode, BOOL fExternal, INT_PTR offset)
        {
            return fExternal;
        }

        virtual VOID EmitInstruction(UINT refSize, __int64 fixedUpReference, BYTE *pOutBuffer, UINT variationCode, BYTE *pDataBuffer, BYTE adjustOffset = 0)
        {
            _ASSERTE(!"MIPS64: not implementation on mips64!!!");
            LIMITED_METHOD_CONTRACT;
            // VariationCode is used to indicate the register the label is going to be loaded

            DWORD imm =(DWORD)(fixedUpReference>>12);
            if (imm>>21)
                COMPlusThrow(kNotSupportedException);

            // Can't use SP or XZR
            _ASSERTE((variationCode & 0x1F) != 31);

            // adrp Xt, #Page_of_fixedUpReference
            *((DWORD*)pOutBuffer) = ((9<<28) | ((imm & 3)<<29) | (imm>>2)<<5 | (variationCode&0x1F));

            // ldr Xt, [Xt, #offset_of_fixedUpReference_to_its_page]
            UINT64 target = (UINT64)(fixedUpReference + pOutBuffer)>>3;
            *((DWORD*)(pOutBuffer+4)) = ( 0xF9400000 | ((target & 0x1FF)<<10) | (variationCode & 0x1F)<<5 | (variationCode & 0x1F));
        }
};



static BYTE gConditionalBranchIF[sizeof(ConditionalBranchInstructionFormat)];
static BYTE gBranchIF[sizeof(BranchInstructionFormat)];
static BYTE gLoadFromLabelIF[sizeof(LoadFromLabelInstructionFormat)];

#endif

void ClearRegDisplayArgumentAndScratchRegisters(REGDISPLAY * pRD)
{
    pRD->volatileCurrContextPointers.At = NULL;
    pRD->volatileCurrContextPointers.V0 = NULL;
    pRD->volatileCurrContextPointers.V1 = NULL;
    pRD->volatileCurrContextPointers.A0 = NULL;
    pRD->volatileCurrContextPointers.A1 = NULL;
    pRD->volatileCurrContextPointers.A2 = NULL;
    pRD->volatileCurrContextPointers.A3 = NULL;
    pRD->volatileCurrContextPointers.A4 = NULL;
    pRD->volatileCurrContextPointers.A5 = NULL;
    pRD->volatileCurrContextPointers.A6 = NULL;
    pRD->volatileCurrContextPointers.A7 = NULL;
    pRD->volatileCurrContextPointers.T0 = NULL;
    pRD->volatileCurrContextPointers.T1 = NULL;
    pRD->volatileCurrContextPointers.T2 = NULL;
    pRD->volatileCurrContextPointers.T3 = NULL;
    pRD->volatileCurrContextPointers.T8 = NULL;
    pRD->volatileCurrContextPointers.T9 = NULL;
}

#ifndef CROSSGEN_COMPILE
void LazyMachState::unwindLazyState(LazyMachState* baseState,
                                    MachState* unwoundstate,
                                    DWORD threadId,
                                    int funCallDepth,
                                    HostCallPreference hostCallPreference)
{
    T_CONTEXT context;
    T_KNONVOLATILE_CONTEXT_POINTERS nonVolContextPtrs;
    context.S0 = unwoundstate->captureCalleeSavedRegisters[0] = baseState->captureCalleeSavedRegisters[0];
    context.S1 = unwoundstate->captureCalleeSavedRegisters[1] = baseState->captureCalleeSavedRegisters[1];
    context.S2 = unwoundstate->captureCalleeSavedRegisters[2] = baseState->captureCalleeSavedRegisters[2];
    context.S3 = unwoundstate->captureCalleeSavedRegisters[3] = baseState->captureCalleeSavedRegisters[3];
    context.S4 = unwoundstate->captureCalleeSavedRegisters[4] = baseState->captureCalleeSavedRegisters[4];
    context.S5 = unwoundstate->captureCalleeSavedRegisters[5] = baseState->captureCalleeSavedRegisters[5];
    context.S6 = unwoundstate->captureCalleeSavedRegisters[6] = baseState->captureCalleeSavedRegisters[6];
    context.S7 = unwoundstate->captureCalleeSavedRegisters[7] = baseState->captureCalleeSavedRegisters[7];
    context.Gp = unwoundstate->captureCalleeSavedRegisters[8] = baseState->captureCalleeSavedRegisters[8];
    context.Fp = unwoundstate->captureCalleeSavedRegisters[9] = baseState->captureCalleeSavedRegisters[9];
    context.Ra = NULL; // Filled by the unwinder

    context.Sp = baseState->captureSp;
    context.Pc = baseState->captureIp;

#if !defined(DACCESS_COMPILE)
    // For DAC, if we get here, it means that the LazyMachState is uninitialized and we have to unwind it.
    // The API we use to unwind in DAC is StackWalk64(), which does not support the context pointers.
    //
    // Restore the integer registers to KNONVOLATILE_CONTEXT_POINTERS to be used for unwinding.
    nonVolContextPtrs.S0 = &unwoundstate->captureCalleeSavedRegisters[0];
    nonVolContextPtrs.S1 = &unwoundstate->captureCalleeSavedRegisters[1];
    nonVolContextPtrs.S2 = &unwoundstate->captureCalleeSavedRegisters[2];
    nonVolContextPtrs.S3 = &unwoundstate->captureCalleeSavedRegisters[3];
    nonVolContextPtrs.S4 = &unwoundstate->captureCalleeSavedRegisters[4];
    nonVolContextPtrs.S5 = &unwoundstate->captureCalleeSavedRegisters[5];
    nonVolContextPtrs.S6 = &unwoundstate->captureCalleeSavedRegisters[6];
    nonVolContextPtrs.S7 = &unwoundstate->captureCalleeSavedRegisters[7];
    nonVolContextPtrs.Gp = &unwoundstate->captureCalleeSavedRegisters[8];
    nonVolContextPtrs.Fp = &unwoundstate->captureCalleeSavedRegisters[9];
    nonVolContextPtrs.Ra = NULL; // Filled by the unwinder

#endif // DACCESS_COMPILE

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    LazyMachState::unwindLazyState(ip:%p,sp:%p)\n", baseState->captureIp, baseState->captureSp));

    PCODE pvControlPc;

    do {

#ifndef FEATURE_PAL
        pvControlPc = Thread::VirtualUnwindCallFrame(&context, &nonVolContextPtrs);
#else // !FEATURE_PAL
#ifdef DACCESS_COMPILE
        HRESULT hr = DacVirtualUnwind(threadId, &context, &nonVolContextPtrs);
        if (FAILED(hr))
        {
            DacError(hr);
        }
#else // DACCESS_COMPILE
        BOOL success = PAL_VirtualUnwind(&context, &nonVolContextPtrs);
        if (!success)
        {
            _ASSERTE(!"unwindLazyState: Unwinding failed");
            EEPOLICY_HANDLE_FATAL_ERROR(COR_E_EXECUTIONENGINE);
        }
#endif // DACCESS_COMPILE
        pvControlPc = GetIP(&context);
#endif // !FEATURE_PAL

        if (funCallDepth > 0)
        {
            funCallDepth--;
            if (funCallDepth == 0)
                break;
        }
        else
        {
            // Determine  whether given IP resides in JITted code. (It returns nonzero in that case.)
            // Use it now to see if we've unwound to managed code yet.
            BOOL fFailedReaderLock = FALSE;
            BOOL fIsManagedCode = ExecutionManager::IsManagedCode(pvControlPc, hostCallPreference, &fFailedReaderLock);
            if (fFailedReaderLock)
            {
                // We don't know if we would have been able to find a JIT
                // manager, because we couldn't enter the reader lock without
                // yielding (and our caller doesn't want us to yield).  So abort
                // now.

                // Invalidate the lazyState we're returning, so the caller knows
                // we aborted before we could fully unwind
                unwoundstate->_isValid = false;
                return;
            }

            if (fIsManagedCode)
                break;

        }
    } while (true);

#ifdef FEATURE_PAL
    unwoundstate->captureCalleeSavedRegisters[0] = context.S0;
    unwoundstate->captureCalleeSavedRegisters[1] = context.S1;
    unwoundstate->captureCalleeSavedRegisters[2] = context.S2;
    unwoundstate->captureCalleeSavedRegisters[3] = context.S3;
    unwoundstate->captureCalleeSavedRegisters[4] = context.S4;
    unwoundstate->captureCalleeSavedRegisters[5] = context.S5;
    unwoundstate->captureCalleeSavedRegisters[6] = context.S6;
    unwoundstate->captureCalleeSavedRegisters[7] = context.S7;
    unwoundstate->captureCalleeSavedRegisters[8] = context.Gp;
    unwoundstate->captureCalleeSavedRegisters[9] = context.Fp;
    unwoundstate->captureCalleeSavedRegisters[10] = context.Ra;
#endif

#ifdef DACCESS_COMPILE
    // For DAC builds, we update the registers directly since we dont have context pointers
    unwoundstate->captureCalleeSavedRegisters[0] = context.S0;
    unwoundstate->captureCalleeSavedRegisters[1] = context.S1;
    unwoundstate->captureCalleeSavedRegisters[2] = context.S2;
    unwoundstate->captureCalleeSavedRegisters[3] = context.S3;
    unwoundstate->captureCalleeSavedRegisters[4] = context.S4;
    unwoundstate->captureCalleeSavedRegisters[5] = context.S5;
    unwoundstate->captureCalleeSavedRegisters[6] = context.S6;
    unwoundstate->captureCalleeSavedRegisters[7] = context.S7;
    unwoundstate->captureCalleeSavedRegisters[8] = context.Gp;
    unwoundstate->captureCalleeSavedRegisters[9] = context.Fp;
    unwoundstate->captureCalleeSavedRegisters[10] = context.Ra;
#else // !DACCESS_COMPILE
    // For non-DAC builds, update the register state from context pointers
    unwoundstate->ptrCalleeSavedRegisters[0] = nonVolContextPtrs.S0;
    unwoundstate->ptrCalleeSavedRegisters[1] = nonVolContextPtrs.S1;
    unwoundstate->ptrCalleeSavedRegisters[2] = nonVolContextPtrs.S2;
    unwoundstate->ptrCalleeSavedRegisters[3] = nonVolContextPtrs.S3;
    unwoundstate->ptrCalleeSavedRegisters[4] = nonVolContextPtrs.S4;
    unwoundstate->ptrCalleeSavedRegisters[5] = nonVolContextPtrs.S5;
    unwoundstate->ptrCalleeSavedRegisters[6] = nonVolContextPtrs.S6;
    unwoundstate->ptrCalleeSavedRegisters[7] = nonVolContextPtrs.S7;
    unwoundstate->ptrCalleeSavedRegisters[8] = nonVolContextPtrs.Gp;
    unwoundstate->ptrCalleeSavedRegisters[9] = nonVolContextPtrs.Fp;
    unwoundstate->ptrCalleeSavedRegisters[10] = nonVolContextPtrs.Ra;
#endif // DACCESS_COMPILE

    unwoundstate->_pc = context.Pc;
    unwoundstate->_sp = context.Sp;

    unwoundstate->_isValid = TRUE;
}

void HelperMethodFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        SUPPORTS_DAC;
    }
    CONTRACTL_END;

    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;        // Don't add usage of this field.  This is only temporary.

    //
    // Copy the saved state from the frame to the current context.
    //

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    HelperMethodFrame::UpdateRegDisplay cached ip:%p, sp:%p\n", m_MachState._pc, m_MachState._sp));

 #if defined(DACCESS_COMPILE)
    // For DAC, we may get here when the HMF is still uninitialized.
    // So we may need to unwind here.
    if (!m_MachState.isValid())
    {
        // This allocation throws on OOM.
        MachState* pUnwoundState = (MachState*)DacAllocHostOnlyInstance(sizeof(*pUnwoundState), true);

        InsureInit(false, pUnwoundState);

        pRD->pCurrentContext->Pc = pRD->ControlPC = pUnwoundState->_pc;
        pRD->pCurrentContext->Sp = pRD->SP        = pUnwoundState->_sp;
        pRD->pCurrentContext->S0 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[0]);
        pRD->pCurrentContext->S1 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[1]);
        pRD->pCurrentContext->S2 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[2]);
        pRD->pCurrentContext->S3 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[3]);
        pRD->pCurrentContext->S4 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[4]);
        pRD->pCurrentContext->S5 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[5]);
        pRD->pCurrentContext->S6 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[6]);
        pRD->pCurrentContext->S7 = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[7]);
        pRD->pCurrentContext->Gp = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[8]);
        pRD->pCurrentContext->Fp = (DWORD64)(pUnwoundState->captureCalleeSavedRegisters[9]);
        pRD->pCurrentContext->Ra = NULL; // Unwind again to get Caller's PC

        pRD->pCurrentContextPointers->S0 = pUnwoundState->ptrCalleeSavedRegisters[0];
        pRD->pCurrentContextPointers->S1 = pUnwoundState->ptrCalleeSavedRegisters[1];
        pRD->pCurrentContextPointers->S2 = pUnwoundState->ptrCalleeSavedRegisters[2];
        pRD->pCurrentContextPointers->S3 = pUnwoundState->ptrCalleeSavedRegisters[3];
        pRD->pCurrentContextPointers->S4 = pUnwoundState->ptrCalleeSavedRegisters[4];
        pRD->pCurrentContextPointers->S5 = pUnwoundState->ptrCalleeSavedRegisters[5];
        pRD->pCurrentContextPointers->S6 = pUnwoundState->ptrCalleeSavedRegisters[6];
        pRD->pCurrentContextPointers->S7 = pUnwoundState->ptrCalleeSavedRegisters[7];
        pRD->pCurrentContextPointers->Gp = pUnwoundState->ptrCalleeSavedRegisters[8];
        pRD->pCurrentContextPointers->Fp = pUnwoundState->ptrCalleeSavedRegisters[9];
        pRD->pCurrentContextPointers->Ra = NULL;
        return;
    }
#endif // DACCESS_COMPILE

    // reset pContext; it's only valid for active (top-most) frame
    pRD->pContext = NULL;
    pRD->ControlPC = GetReturnAddress(); // m_MachState._pc;
    pRD->SP = (DWORD64)(size_t)m_MachState._sp;

    pRD->pCurrentContext->Pc = pRD->ControlPC;
    pRD->pCurrentContext->Sp = pRD->SP;

#ifdef FEATURE_PAL
    pRD->pCurrentContext->S0 = m_MachState.ptrCalleeSavedRegisters[0] ? *m_MachState.ptrCalleeSavedRegisters[0] : m_MachState.captureCalleeSavedRegisters[0];
    pRD->pCurrentContext->S1 = m_MachState.ptrCalleeSavedRegisters[1] ? *m_MachState.ptrCalleeSavedRegisters[1] : m_MachState.captureCalleeSavedRegisters[1];
    pRD->pCurrentContext->S2 = m_MachState.ptrCalleeSavedRegisters[2] ? *m_MachState.ptrCalleeSavedRegisters[2] : m_MachState.captureCalleeSavedRegisters[2];
    pRD->pCurrentContext->S3 = m_MachState.ptrCalleeSavedRegisters[3] ? *m_MachState.ptrCalleeSavedRegisters[3] : m_MachState.captureCalleeSavedRegisters[3];
    pRD->pCurrentContext->S4 = m_MachState.ptrCalleeSavedRegisters[4] ? *m_MachState.ptrCalleeSavedRegisters[4] : m_MachState.captureCalleeSavedRegisters[4];
    pRD->pCurrentContext->S5 = m_MachState.ptrCalleeSavedRegisters[5] ? *m_MachState.ptrCalleeSavedRegisters[5] : m_MachState.captureCalleeSavedRegisters[5];
    pRD->pCurrentContext->S6 = m_MachState.ptrCalleeSavedRegisters[6] ? *m_MachState.ptrCalleeSavedRegisters[6] : m_MachState.captureCalleeSavedRegisters[6];
    pRD->pCurrentContext->S7 = m_MachState.ptrCalleeSavedRegisters[7] ? *m_MachState.ptrCalleeSavedRegisters[7] : m_MachState.captureCalleeSavedRegisters[7];
    pRD->pCurrentContext->Gp = m_MachState.ptrCalleeSavedRegisters[8] ? *m_MachState.ptrCalleeSavedRegisters[8] : m_MachState.captureCalleeSavedRegisters[8];
    pRD->pCurrentContext->Fp = m_MachState.ptrCalleeSavedRegisters[9] ? *m_MachState.ptrCalleeSavedRegisters[9] : m_MachState.captureCalleeSavedRegisters[9];
    pRD->pCurrentContext->Ra = NULL; // Unwind again to get Caller's PC
#else // FEATURE_PAL
    pRD->pCurrentContext->S0 = *m_MachState.ptrCalleeSavedRegisters[0];
    pRD->pCurrentContext->S1 = *m_MachState.ptrCalleeSavedRegisters[1];
    pRD->pCurrentContext->S2 = *m_MachState.ptrCalleeSavedRegisters[2];
    pRD->pCurrentContext->S3 = *m_MachState.ptrCalleeSavedRegisters[3];
    pRD->pCurrentContext->S4 = *m_MachState.ptrCalleeSavedRegisters[4];
    pRD->pCurrentContext->S5 = *m_MachState.ptrCalleeSavedRegisters[5];
    pRD->pCurrentContext->S6 = *m_MachState.ptrCalleeSavedRegisters[6];
    pRD->pCurrentContext->S7 = *m_MachState.ptrCalleeSavedRegisters[7];
    pRD->pCurrentContext->Gp = *m_MachState.ptrCalleeSavedRegisters[8];
    pRD->pCurrentContext->Fp = *m_MachState.ptrCalleeSavedRegisters[9];
    pRD->pCurrentContext->Ra = NULL; // Unwind again to get Caller's PC
#endif

#if !defined(DACCESS_COMPILE)
    pRD->pCurrentContextPointers->S0 = m_MachState.ptrCalleeSavedRegisters[0];
    pRD->pCurrentContextPointers->S1 = m_MachState.ptrCalleeSavedRegisters[1];
    pRD->pCurrentContextPointers->S2 = m_MachState.ptrCalleeSavedRegisters[2];
    pRD->pCurrentContextPointers->S3 = m_MachState.ptrCalleeSavedRegisters[3];
    pRD->pCurrentContextPointers->S4 = m_MachState.ptrCalleeSavedRegisters[4];
    pRD->pCurrentContextPointers->S5 = m_MachState.ptrCalleeSavedRegisters[5];
    pRD->pCurrentContextPointers->S6 = m_MachState.ptrCalleeSavedRegisters[6];
    pRD->pCurrentContextPointers->S7 = m_MachState.ptrCalleeSavedRegisters[7];
    pRD->pCurrentContextPointers->Gp = m_MachState.ptrCalleeSavedRegisters[8];
    pRD->pCurrentContextPointers->Fp = m_MachState.ptrCalleeSavedRegisters[9];
    pRD->pCurrentContextPointers->Ra = NULL; // Unwind again to get Caller's PC
#endif
    ClearRegDisplayArgumentAndScratchRegisters(pRD);
}
#endif // CROSSGEN_COMPILE

TADDR FixupPrecode::GetMethodDesc()
{
    LIMITED_METHOD_DAC_CONTRACT;

    // This lookup is also manually inlined in PrecodeFixupThunk assembly code
    TADDR base = *PTR_TADDR(GetBase());
    if (base == NULL)
        return NULL;
    return base + (m_MethodDescChunkIndex * MethodDesc::ALIGNMENT);
}

#ifdef DACCESS_COMPILE
void FixupPrecode::EnumMemoryRegions(CLRDataEnumMemoryFlags flags)
{
	SUPPORTS_DAC;
	DacEnumMemoryRegion(dac_cast<TADDR>(this), sizeof(FixupPrecode));

	DacEnumMemoryRegion(GetBase(), sizeof(TADDR));
}
#endif // DACCESS_COMPILE

#ifndef DACCESS_COMPILE
void StubPrecode::Init(MethodDesc* pMD, LoaderAllocator *pLoaderAllocator)
{
    WRAPPER_NO_CONTRACT;

    int n = 0;
    m_rgCode[n++] = 0xdf2e0018; //ld  t2, 24(t9)
    m_rgCode[n++] = 0xdf390010; //ld  t9, 16(t9)
    m_rgCode[n++] = 0x03200008; //jr  t9
    m_rgCode[n++] = 0x34000071; //ori zero,zero,0x71   //for Type encoding.
    _ASSERTE(n == _countof(m_rgCode));

    m_pTarget = GetPreStubEntryPoint();
    m_pMethodDesc = (TADDR)pMD;
}

#ifdef FEATURE_NATIVE_IMAGE_GENERATION
void StubPrecode::Fixup(DataImage *image)
{
    WRAPPER_NO_CONTRACT;

    image->FixupFieldToNode(this, offsetof(StubPrecode, m_pTarget),
                            image->GetHelperThunk(CORINFO_HELP_EE_PRESTUB),
                            0,
                            IMAGE_REL_BASED_PTR);

    image->FixupField(this, offsetof(StubPrecode, m_pMethodDesc),
                      (void*)GetMethodDesc(),
                      0,
                      IMAGE_REL_BASED_PTR);
}
#endif // FEATURE_NATIVE_IMAGE_GENERATION

void NDirectImportPrecode::Init(MethodDesc* pMD, LoaderAllocator *pLoaderAllocator)
{
    WRAPPER_NO_CONTRACT;

    int n = 0;

    m_rgCode[n++] = 0xdf2e0018; // ld  t2,24(t9)          ; =m_pMethodDesc
    m_rgCode[n++] = 0xdf390010; // ld  t9,16(t9)          ; =m_pTarget
    m_rgCode[n++] = 0x03200008; // jr  t9
    m_rgCode[n++] = 0x34000072; // ori zero,zero,0x72    //for Type encoding.
    _ASSERTE(n == _countof(m_rgCode));

    m_pTarget = GetEEFuncEntryPoint(NDirectImportThunk);
    m_pMethodDesc = (TADDR)pMD;
}

#ifdef FEATURE_NATIVE_IMAGE_GENERATION
void NDirectImportPrecode::Fixup(DataImage *image)
{
    WRAPPER_NO_CONTRACT;

    image->FixupField(this, offsetof(NDirectImportPrecode, m_pMethodDesc),
                      (void*)GetMethodDesc(),
                      0,
                      IMAGE_REL_BASED_PTR);

    image->FixupFieldToNode(this, offsetof(NDirectImportPrecode, m_pTarget),
                            image->GetHelperThunk(CORINFO_HELP_EE_PINVOKE_FIXUP),
                            0,
                            IMAGE_REL_BASED_PTR);
}
#endif

void FixupPrecode::Init(MethodDesc* pMD, LoaderAllocator *pLoaderAllocator, int iMethodDescChunkIndex /*=0*/, int iPrecodeChunkIndex /*=0*/)
{
    WRAPPER_NO_CONTRACT;

    InitCommon();

    // Initialize chunk indices only if they are not initialized yet. This is necessary to make MethodDesc::Reset work.
    if (m_PrecodeChunkIndex == 0)
    {
        _ASSERTE(FitsInU1(iPrecodeChunkIndex));
        m_PrecodeChunkIndex = static_cast<BYTE>(iPrecodeChunkIndex);
    }

    if (iMethodDescChunkIndex != -1)
    {
        if (m_MethodDescChunkIndex == 0)
        {
            _ASSERTE(FitsInU1(iMethodDescChunkIndex));
            m_MethodDescChunkIndex = static_cast<BYTE>(iMethodDescChunkIndex);
        }

        if (*(void**)GetBase() == NULL)
            *(void**)GetBase() = (BYTE*)pMD - (iMethodDescChunkIndex * MethodDesc::ALIGNMENT);
    }

    _ASSERTE(GetMethodDesc() == (TADDR)pMD);

    if (pLoaderAllocator != NULL)
    {
        m_pTarget = GetEEFuncEntryPoint(PrecodeFixupThunk);
    }
}

#ifdef FEATURE_NATIVE_IMAGE_GENERATION
// Partial initialization. Used to save regrouped chunks.
void FixupPrecode::InitForSave(int iPrecodeChunkIndex)
{
    STANDARD_VM_CONTRACT;

    InitCommon();

    _ASSERTE(FitsInU1(iPrecodeChunkIndex));
    m_PrecodeChunkIndex = static_cast<BYTE>(iPrecodeChunkIndex);
    // The rest is initialized in code:FixupPrecode::Fixup
}

void FixupPrecode::Fixup(DataImage *image, MethodDesc * pMD)
{
    STANDARD_VM_CONTRACT;

    // Note that GetMethodDesc() does not return the correct value because of
    // regrouping of MethodDescs into hot and cold blocks. That's why the caller
    // has to supply the actual MethodDesc

    SSIZE_T mdChunkOffset;
    ZapNode * pMDChunkNode = image->GetNodeForStructure(pMD, &mdChunkOffset);
    ZapNode * pHelperThunk = image->GetHelperThunk(CORINFO_HELP_EE_PRECODE_FIXUP);

    image->FixupFieldToNode(this, offsetof(FixupPrecode, m_pTarget), pHelperThunk);

    // Set the actual chunk index
    FixupPrecode * pNewPrecode = (FixupPrecode *)image->GetImagePointer(this);

    size_t mdOffset = mdChunkOffset - sizeof(MethodDescChunk);
    size_t chunkIndex = mdOffset / MethodDesc::ALIGNMENT;
    _ASSERTE(FitsInU1(chunkIndex));
    pNewPrecode->m_MethodDescChunkIndex = (BYTE)chunkIndex;

    // Fixup the base of MethodDescChunk
    if (m_PrecodeChunkIndex == 0)
    {
        image->FixupFieldToNode(this, (BYTE *)GetBase() - (BYTE *)this,
            pMDChunkNode, sizeof(MethodDescChunk));
    }
}
#endif // FEATURE_NATIVE_IMAGE_GENERATION


void ThisPtrRetBufPrecode::Init(MethodDesc* pMD, LoaderAllocator *pLoaderAllocator)
{
    WRAPPER_NO_CONTRACT;

    int n = 0;
    //Initially
    //a0 -This ptr
    //a1 -ReturnBuffer
    m_rgCode[n++] = 0xdf390018; //ld  t9,24(t9)
    m_rgCode[n++] = 0x348f0000; //ori t3,a0,0x0
    m_rgCode[n++] = 0x34a40000; //ori a0,a1,0x0
    m_rgCode[n++] = 0x03200008; //jr  t9
    m_rgCode[n++] = 0x35e50000; //ori a1,t3,0x0
    m_rgCode[n++] = 0; //nop

    _ASSERTE((UINT32*)&m_pTarget == &m_rgCode[n]);
    _ASSERTE(n == _countof(m_rgCode));

    m_pTarget = GetPreStubEntryPoint();
    m_pMethodDesc = (TADDR)pMD;
}

#ifndef CROSSGEN_COMPILE
BOOL DoesSlotCallPrestub(PCODE pCode)
{
    PTR_DWORD pInstr = dac_cast<PTR_DWORD>(PCODEToPINSTR(pCode));

    //FixupPrecode
#if defined(HAS_FIXUP_PRECODE)
    if (FixupPrecode::IsFixupPrecodeByASM(pCode))
    {
        PCODE pTarget = dac_cast<PTR_FixupPrecode>(pInstr)->m_pTarget;

        if (isJump(pTarget))
        {
            pTarget = decodeJump(pTarget);
        }

        return pTarget == (TADDR)PrecodeFixupThunk;
    }
#endif

    // StubPrecode
    if (pInstr[0] == 0xdf2e0018 && //ld  t2, 24(t9)
        pInstr[1] == 0xdf390010 && //ld  t9, 16(t9)
        pInstr[2] == 0x03200008 && //jr  t9
        pInstr[3] == 0x34000071)   //ori zero,zero,0x71
    {
        PCODE pTarget = dac_cast<PTR_StubPrecode>(pInstr)->m_pTarget;

        if (isJump(pTarget))
        {
            pTarget = decodeJump(pTarget);
        }

        return pTarget == GetPreStubEntryPoint();
    }

    return FALSE;

}

#endif // CROSSGEN_COMPILE

#endif // !DACCESS_COMPILE

void UpdateRegDisplayFromCalleeSavedRegisters(REGDISPLAY * pRD, CalleeSavedRegisters * pCalleeSaved)
{
    LIMITED_METHOD_CONTRACT;
    pRD->pCurrentContext->S0 = pCalleeSaved->s0;
    pRD->pCurrentContext->S1 = pCalleeSaved->s1;
    pRD->pCurrentContext->S2 = pCalleeSaved->s2;
    pRD->pCurrentContext->S3 = pCalleeSaved->s3;
    pRD->pCurrentContext->S4 = pCalleeSaved->s4;
    pRD->pCurrentContext->S5 = pCalleeSaved->s5;
    pRD->pCurrentContext->S6 = pCalleeSaved->s6;
    pRD->pCurrentContext->S7 = pCalleeSaved->s7;
    pRD->pCurrentContext->Gp = pCalleeSaved->gp;
    pRD->pCurrentContext->Fp  = pCalleeSaved->fp;
    pRD->pCurrentContext->Ra  = pCalleeSaved->ra;

    T_KNONVOLATILE_CONTEXT_POINTERS * pContextPointers = pRD->pCurrentContextPointers;
    pContextPointers->S0 = (PDWORD64)&pCalleeSaved->s0;
    pContextPointers->S1 = (PDWORD64)&pCalleeSaved->s1;
    pContextPointers->S2 = (PDWORD64)&pCalleeSaved->s2;
    pContextPointers->S3 = (PDWORD64)&pCalleeSaved->s3;
    pContextPointers->S4 = (PDWORD64)&pCalleeSaved->s4;
    pContextPointers->S5 = (PDWORD64)&pCalleeSaved->s5;
    pContextPointers->S6 = (PDWORD64)&pCalleeSaved->s6;
    pContextPointers->S7 = (PDWORD64)&pCalleeSaved->s7;
    pContextPointers->Gp = (PDWORD64)&pCalleeSaved->gp;
    pContextPointers->Fp = (PDWORD64)&pCalleeSaved->fp;
    pContextPointers->Ra  = (PDWORD64)&pCalleeSaved->ra;
}

#ifndef CROSSGEN_COMPILE

void TransitionFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;        // Don't add usage of this field.  This is only temporary.

    // copy the callee saved regs
    CalleeSavedRegisters *pCalleeSaved = GetCalleeSavedRegisters();
    UpdateRegDisplayFromCalleeSavedRegisters(pRD, pCalleeSaved);

    ClearRegDisplayArgumentAndScratchRegisters(pRD);

    // copy the control registers
    pRD->pCurrentContext->Fp = pCalleeSaved->fp;
    pRD->pCurrentContext->Ra = pCalleeSaved->ra;
    pRD->pCurrentContext->Pc = GetReturnAddress();
    pRD->pCurrentContext->Sp = this->GetSP();

    // Finally, syncup the regdisplay with the context
    SyncRegDisplayToCurrentContext(pRD);

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    TransitionFrame::UpdateRegDisplay(pc:%p, sp:%p)\n", pRD->ControlPC, pRD->SP));
}


#endif

#ifndef	CROSSGEN_COMPILE

void TailCallFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    TailCallFrame::UpdateRegDisplay(pc:%p, sp:%p)\n", pRD->ControlPC, pRD->SP));
    _ASSERTE(!"MIPS64:NYI");
}

#ifndef DACCESS_COMPILE
void TailCallFrame::InitFromContext(T_CONTEXT * pContext)
{
    _ASSERTE(!"MIPS64:NYI");
}
#endif // !DACCESS_COMPILE

#endif // CROSSGEN_COMPILE

void FaultingExceptionFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    LIMITED_METHOD_DAC_CONTRACT;

    // Copy the context to regdisplay
    memcpy(pRD->pCurrentContext, &m_ctx, sizeof(T_CONTEXT));

    pRD->ControlPC = ::GetIP(&m_ctx);
    pRD->SP = ::GetSP(&m_ctx);

    // Update the integer registers in KNONVOLATILE_CONTEXT_POINTERS from
    // the exception context we have.
    pRD->pCurrentContextPointers->S0 = (PDWORD64)&m_ctx.S0;
    pRD->pCurrentContextPointers->S1 = (PDWORD64)&m_ctx.S1;
    pRD->pCurrentContextPointers->S2 = (PDWORD64)&m_ctx.S2;
    pRD->pCurrentContextPointers->S3 = (PDWORD64)&m_ctx.S3;
    pRD->pCurrentContextPointers->S4 = (PDWORD64)&m_ctx.S4;
    pRD->pCurrentContextPointers->S5 = (PDWORD64)&m_ctx.S5;
    pRD->pCurrentContextPointers->S6 = (PDWORD64)&m_ctx.S6;
    pRD->pCurrentContextPointers->S7 = (PDWORD64)&m_ctx.S7;
    pRD->pCurrentContextPointers->Gp = (PDWORD64)&m_ctx.Gp;
    pRD->pCurrentContextPointers->Fp = (PDWORD64)&m_ctx.Fp;
    pRD->pCurrentContextPointers->Ra = (PDWORD64)&m_ctx.Ra;

    ClearRegDisplayArgumentAndScratchRegisters(pRD);

    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;        // Don't add usage of this field.  This is only temporary.

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    FaultingExceptionFrame::UpdateRegDisplay(pc:%p, sp:%p)\n", pRD->ControlPC, pRD->SP));
}

void InlinedCallFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    CONTRACT_VOID
    {
        NOTHROW;
        GC_NOTRIGGER;
#ifdef PROFILING_SUPPORTED
        PRECONDITION(CORProfilerStackSnapshotEnabled() || InlinedCallFrame::FrameHasActiveCall(this));
#endif
        HOST_NOCALLS;
        MODE_ANY;
        SUPPORTS_DAC;
    }
    CONTRACT_END;

    if (!InlinedCallFrame::FrameHasActiveCall(this))
    {
        LOG((LF_CORDB, LL_ERROR, "WARNING: InlinedCallFrame::UpdateRegDisplay called on inactive frame %p\n", this));
        return;
    }

    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;

    pRD->pCurrentContext->Pc = *(DWORD64 *)&m_pCallerReturnAddress;
    pRD->pCurrentContext->Sp = *(DWORD64 *)&m_pCallSiteSP;
    pRD->pCurrentContext->Fp = *(DWORD64 *)&m_pCalleeSavedFP;

    pRD->pCurrentContextPointers->S0 = NULL;
    pRD->pCurrentContextPointers->S1 = NULL;
    pRD->pCurrentContextPointers->S2 = NULL;
    pRD->pCurrentContextPointers->S3 = NULL;
    pRD->pCurrentContextPointers->S4 = NULL;
    pRD->pCurrentContextPointers->S5 = NULL;
    pRD->pCurrentContextPointers->S6 = NULL;
    pRD->pCurrentContextPointers->S7 = NULL;
    pRD->pCurrentContextPointers->Gp = NULL;

    pRD->ControlPC = m_pCallerReturnAddress;
    pRD->SP = (DWORD64) dac_cast<TADDR>(m_pCallSiteSP);

    // reset pContext; it's only valid for active (top-most) frame
    pRD->pContext = NULL;

    ClearRegDisplayArgumentAndScratchRegisters(pRD);


    // Update the frame pointer in the current context.
    pRD->pCurrentContextPointers->Fp = &m_pCalleeSavedFP;

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    InlinedCallFrame::UpdateRegDisplay(pc:%p, sp:%p)\n", pRD->ControlPC, pRD->SP));

    RETURN;
}

#ifdef FEATURE_HIJACK
TADDR ResumableFrame::GetReturnAddressPtr(void)
{
    LIMITED_METHOD_DAC_CONTRACT;
    return dac_cast<TADDR>(m_Regs) + offsetof(T_CONTEXT, Pc);
}

void ResumableFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    CONTRACT_VOID
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        SUPPORTS_DAC;
    }
    CONTRACT_END;

    CopyMemory(pRD->pCurrentContext, m_Regs, sizeof(T_CONTEXT));

    pRD->ControlPC = m_Regs->Pc;
    pRD->SP = m_Regs->Sp;

    pRD->pCurrentContextPointers->S0 = &m_Regs->S0;
    pRD->pCurrentContextPointers->S1 = &m_Regs->S1;
    pRD->pCurrentContextPointers->S2 = &m_Regs->S2;
    pRD->pCurrentContextPointers->S3 = &m_Regs->S3;
    pRD->pCurrentContextPointers->S4 = &m_Regs->S4;
    pRD->pCurrentContextPointers->S5 = &m_Regs->S5;
    pRD->pCurrentContextPointers->S6 = &m_Regs->S6;
    pRD->pCurrentContextPointers->S7 = &m_Regs->S7;
    pRD->pCurrentContextPointers->Gp = &m_Regs->Gp;
    pRD->pCurrentContextPointers->Fp = &m_Regs->Fp;
    pRD->pCurrentContextPointers->Ra = &m_Regs->Ra;

    pRD->volatileCurrContextPointers.At = &m_Regs->At;
    pRD->volatileCurrContextPointers.V0 = &m_Regs->V0;
    pRD->volatileCurrContextPointers.V1 = &m_Regs->V1;
    pRD->volatileCurrContextPointers.A0 = &m_Regs->A0;
    pRD->volatileCurrContextPointers.A1 = &m_Regs->A1;
    pRD->volatileCurrContextPointers.A2 = &m_Regs->A2;
    pRD->volatileCurrContextPointers.A3 = &m_Regs->A3;
    pRD->volatileCurrContextPointers.A4 = &m_Regs->A4;
    pRD->volatileCurrContextPointers.A5 = &m_Regs->A5;
    pRD->volatileCurrContextPointers.A6 = &m_Regs->A6;
    pRD->volatileCurrContextPointers.A7 = &m_Regs->A7;
    pRD->volatileCurrContextPointers.T0 = &m_Regs->T0;
    pRD->volatileCurrContextPointers.T1 = &m_Regs->T1;
    pRD->volatileCurrContextPointers.T2 = &m_Regs->T2;
    pRD->volatileCurrContextPointers.T3 = &m_Regs->T3;
    pRD->volatileCurrContextPointers.T8 = &m_Regs->T8;
    pRD->volatileCurrContextPointers.T9 = &m_Regs->T9;

    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;        // Don't add usage of this field.  This is only temporary.

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    ResumableFrame::UpdateRegDisplay(pc:%p, sp:%p)\n", pRD->ControlPC, pRD->SP));

    RETURN;
}

void HijackFrame::UpdateRegDisplay(const PREGDISPLAY pRD)
{
    LIMITED_METHOD_CONTRACT;

    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;

    pRD->pCurrentContext->Pc = m_ReturnAddress;
    size_t s = sizeof(struct HijackArgs);
    _ASSERTE(s%8 == 0); // HijackArgs contains register values and hence will be a multiple of 8
    // stack must be multiple of 16. So if s is not multiple of 16 then there must be padding of 8 bytes
    s = s + s%16;
    pRD->pCurrentContext->Sp = PTR_TO_TADDR(m_Args) + s ;

    pRD->pCurrentContext->V0 = m_Args->V0;

    pRD->pCurrentContext->S0 = m_Args->S0;
    pRD->pCurrentContext->S1 = m_Args->S1;
    pRD->pCurrentContext->S2 = m_Args->S2;
    pRD->pCurrentContext->S3 = m_Args->S3;
    pRD->pCurrentContext->S4 = m_Args->S4;
    pRD->pCurrentContext->S5 = m_Args->S5;
    pRD->pCurrentContext->S6 = m_Args->S6;
    pRD->pCurrentContext->S7 = m_Args->S7;
    pRD->pCurrentContext->Gp = m_Args->Gp;
    pRD->pCurrentContext->Fp = m_Args->Fp;
    pRD->pCurrentContext->Ra = m_Args->Ra;

    pRD->pCurrentContextPointers->S0 = &m_Args->S0;
    pRD->pCurrentContextPointers->S1 = &m_Args->S1;
    pRD->pCurrentContextPointers->S2 = &m_Args->S2;
    pRD->pCurrentContextPointers->S3 = &m_Args->S3;
    pRD->pCurrentContextPointers->S4 = &m_Args->S4;
    pRD->pCurrentContextPointers->S5 = &m_Args->S5;
    pRD->pCurrentContextPointers->S6 = &m_Args->S6;
    pRD->pCurrentContextPointers->S7 = &m_Args->S7;
    pRD->pCurrentContextPointers->Gp = &m_Args->Gp;
    pRD->pCurrentContextPointers->Fp = &m_Args->Fp;
    pRD->pCurrentContextPointers->Ra = NULL;
    SyncRegDisplayToCurrentContext(pRD);

    LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    HijackFrame::UpdateRegDisplay(pc:%p, sp:%p)\n", pRD->ControlPC, pRD->SP));
}
#endif // FEATURE_HIJACK

#ifdef FEATURE_COMINTEROP

void emitCOMStubCall (ComCallMethodDesc *pCOMMethod, PCODE target)
{
    WRAPPER_NO_CONTRACT;

	// adr x12, label_comCallMethodDesc
	// ldr x10, label_target
	// br x10
	// 4 byte padding for alignment
	// label_target:
    // target address (8 bytes)
    // label_comCallMethodDesc:
    DWORD rgCode[] = {
        0x100000cc,
        0x5800006a,
        0xd61f0140
    };

    BYTE *pBuffer = (BYTE*)pCOMMethod - COMMETHOD_CALL_PRESTUB_SIZE;

    memcpy(pBuffer, rgCode, sizeof(rgCode));
    *((PCODE*)(pBuffer + sizeof(rgCode) + 4)) = target;

    // Ensure that the updated instructions get actually written
    ClrFlushInstructionCache(pBuffer, COMMETHOD_CALL_PRESTUB_SIZE);

    _ASSERTE(IS_ALIGNED(pBuffer + COMMETHOD_CALL_PRESTUB_ADDRESS_OFFSET, sizeof(void*)) &&
             *((PCODE*)(pBuffer + COMMETHOD_CALL_PRESTUB_ADDRESS_OFFSET)) == target);
}
#endif // FEATURE_COMINTEROP

void JIT_ProfilerEnterLeaveTailcallStub(UINT_PTR ProfilerHandle)
{
    _ASSERTE(!"MIPS64:NYI");
}

void JIT_TailCall()
{
    _ASSERTE(!"MIPS64:NYI");
}

#if !defined(DACCESS_COMPILE) && !defined(CROSSGEN_COMPILE)
void InitJITHelpers1()
{
    STANDARD_VM_CONTRACT;

    _ASSERTE(g_SystemInfo.dwNumberOfProcessors != 0);

    // Allocation helpers, faster but non-logging
    if (!((TrackAllocationsEnabled()) ||
        (LoggingOn(LF_GCALLOC, LL_INFO10))
#ifdef _DEBUG
        || (g_pConfig->ShouldInjectFault(INJECTFAULT_GCHEAP) != 0)
#endif // _DEBUG
        ))
    {
        if (GCHeapUtilities::UseThreadAllocationContexts())
        {
            SetJitHelperFunction(CORINFO_HELP_NEWSFAST, JIT_NewS_MP_FastPortable);
            SetJitHelperFunction(CORINFO_HELP_NEWSFAST_ALIGN8, JIT_NewS_MP_FastPortable);
            SetJitHelperFunction(CORINFO_HELP_NEWARR_1_VC, JIT_NewArr1VC_MP_FastPortable);
            SetJitHelperFunction(CORINFO_HELP_NEWARR_1_OBJ, JIT_NewArr1OBJ_MP_FastPortable);

            ECall::DynamicallyAssignFCallImpl(GetEEFuncEntryPoint(AllocateString_MP_FastPortable), ECall::FastAllocateString);
        }
    }

    JIT_UpdateWriteBarrierState(GCHeapUtilities::IsServerHeap());
}

#else
EXTERN_C void JIT_UpdateWriteBarrierState(bool) {}
#endif // !defined(DACCESS_COMPILE) && !defined(CROSSGEN_COMPILE)

EXTERN_C void __stdcall ProfileEnterNaked(UINT_PTR clientData)
{
    _ASSERTE(!"MIPS64:NYI");
}
EXTERN_C void __stdcall ProfileLeaveNaked(UINT_PTR clientData)
{
    _ASSERTE(!"MIPS64:NYI");
}
EXTERN_C void __stdcall ProfileTailcallNaked(UINT_PTR clientData)
{
    _ASSERTE(!"MIPS64:NYI");
}

PTR_CONTEXT GetCONTEXTFromRedirectedStubStackFrame(T_DISPATCHER_CONTEXT * pDispatcherContext)
{
    LIMITED_METHOD_DAC_CONTRACT;

    DWORD64 stackSlot = pDispatcherContext->EstablisherFrame + REDIRECTSTUB_SP_OFFSET_CONTEXT;
    PTR_PTR_CONTEXT ppContext = dac_cast<PTR_PTR_CONTEXT>((TADDR)stackSlot);
    return *ppContext;
}

PTR_CONTEXT GetCONTEXTFromRedirectedStubStackFrame(T_CONTEXT * pContext)
{
    LIMITED_METHOD_DAC_CONTRACT;

    DWORD64 stackSlot = pContext->Sp + REDIRECTSTUB_SP_OFFSET_CONTEXT;
    PTR_PTR_CONTEXT ppContext = dac_cast<PTR_PTR_CONTEXT>((TADDR)stackSlot);
    return *ppContext;
}

void RedirectForThreadAbort()
{
    // ThreadAbort is not supported in .net core
    throw "NYI";
}

#if !defined(DACCESS_COMPILE) && !defined (CROSSGEN_COMPILE)
FaultingExceptionFrame *GetFrameFromRedirectedStubStackFrame (DISPATCHER_CONTEXT *pDispatcherContext)
{
    LIMITED_METHOD_CONTRACT;

    return (FaultingExceptionFrame*)((TADDR)pDispatcherContext->ContextRecord->S0);
}


BOOL
AdjustContextForVirtualStub(
        EXCEPTION_RECORD *pExceptionRecord,
        CONTEXT *pContext)
{
    LIMITED_METHOD_CONTRACT;

    Thread * pThread = GetThread();

    // We may not have a managed thread object. Example is an AV on the helper thread.
    // (perhaps during StubManager::IsStub)
    if (pThread == NULL)
    {
        return FALSE;
    }

    PCODE f_IP = GetIP(pContext);

    VirtualCallStubManager::StubKind sk;
    VirtualCallStubManager::FindStubManager(f_IP, &sk);

    if (sk == VirtualCallStubManager::SK_DISPATCH)
    {
        if (*PTR_DWORD(f_IP) != DISPATCH_STUB_FIRST_DWORD)
        {
            _ASSERTE(!"AV in DispatchStub at unknown instruction");
            return FALSE;
        }
    }
    else
    if (sk == VirtualCallStubManager::SK_RESOLVE)
    {
        if (*PTR_DWORD(f_IP) != RESOLVE_STUB_FIRST_DWORD)
        {
            _ASSERTE(!"AV in ResolveStub at unknown instruction");
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }

    PCODE callsite = GetAdjustedCallAddress(GetRA(pContext));

    // Lr must already have been saved before calling so it should not be necessary to restore Lr

    pExceptionRecord->ExceptionAddress = (PVOID)callsite;
    SetIP(pContext, callsite);

    return TRUE;
}
#endif // !(DACCESS_COMPILE && CROSSGEN_COMPILE)

UMEntryThunk * UMEntryThunk::Decode(void *pCallback)
{
    _ASSERTE(offsetof(UMEntryThunkCode, m_code) == 0);
    UMEntryThunkCode * pCode = (UMEntryThunkCode*)pCallback;

    // We may be called with an unmanaged external code pointer instead. So if it doesn't look like one of our
    // stubs (see UMEntryThunkCode::Encode below) then we'll return NULL. Luckily in these scenarios our
    // caller will perform a hash lookup on successful return to verify our result in case random unmanaged
    // code happens to look like ours.
    if ((pCode->m_code[0] == 0xdf2e0018) &&
        (pCode->m_code[1] == 0xdf390010) &&
        (pCode->m_code[2] == 0x03200008) &&
        (pCode->m_code[3] == 0))
    {
        return (UMEntryThunk*)pCode->m_pvSecretParam;
    }

    return NULL;
}

void UMEntryThunkCode::Encode(BYTE* pTargetCode, void* pvSecretParam)
{
    // ld t2,24(t9)
    // ld t9,16(t9)
    // jr t9
    // nop
    // m_pTargetCode data
    // m_pvSecretParam data

    m_code[0] = 0xdf2e0018; //ld t2,24(t9)
    m_code[1] = 0xdf390010; //ld t9,16(t9)
    m_code[2] = 0x03200008; //jr t9
    m_code[3] = 0; //nop

    m_pTargetCode = (TADDR)pTargetCode;
    m_pvSecretParam = (TADDR)pvSecretParam;

    FlushInstructionCache(GetCurrentProcess(),&m_code,sizeof(m_code));
}

#ifndef DACCESS_COMPILE

void UMEntryThunkCode::Poison()
{
    m_pTargetCode = (TADDR)UMEntryThunk::ReportViolation;

    // ld a0, 24(t9)
    m_code[0] = 0xdf240018;

    ClrFlushInstructionCache(&m_code,sizeof(m_code));
}

#endif // DACCESS_COMPILE

#ifdef PROFILING_SUPPORTED
#include "proftoeeinterfaceimpl.h"

extern UINT_PTR ProfileGetIPFromPlatformSpecificHandle(void * handle)
{
    _ASSERTE(!"MIPS64:NYI");
    return NULL;
}

extern void ProfileSetFunctionIDInPlatformSpecificHandle(void * pPlatformSpecificHandle, FunctionID functionID)
{
    _ASSERTE(!"MIPS64:NYI");
}

ProfileArgIterator::ProfileArgIterator(MetaSig * pMetaSig, void* platformSpecificHandle)
    : m_argIterator(pMetaSig)
{
    _ASSERTE(!"MIPS64:NYI");
}

ProfileArgIterator::~ProfileArgIterator()
{
    _ASSERTE(!"MIPS64:NYI");
}

LPVOID ProfileArgIterator::GetNextArgAddr()
{
    _ASSERTE(!"MIPS64:NYI");
    return NULL;
}

LPVOID ProfileArgIterator::GetHiddenArgValue(void)
{
    _ASSERTE(!"MIPS64:NYI");
    return NULL;
}

LPVOID ProfileArgIterator::GetThis(void)
{
    _ASSERTE(!"MIPS64:NYI");
    return NULL;
}

LPVOID ProfileArgIterator::GetReturnBufferAddr(void)
{
    _ASSERTE(!"MIPS64:NYI");
    return NULL;
}
#endif

#if !defined(DACCESS_COMPILE)
VOID ResetCurrentContext()
{
    LIMITED_METHOD_CONTRACT;
}
#endif

LONG CLRNoCatchHandler(EXCEPTION_POINTERS* pExceptionInfo, PVOID pv)
{
    return EXCEPTION_CONTINUE_SEARCH;
}

void FlushWriteBarrierInstructionCache()
{
    // this wouldn't be called in mips64, just to comply with gchelpers.h
}

#ifndef CROSSGEN_COMPILE
int StompWriteBarrierEphemeral(bool isRuntimeSuspended)
{
    JIT_UpdateWriteBarrierState(GCHeapUtilities::IsServerHeap());
    return SWB_PASS;
}

int StompWriteBarrierResize(bool isRuntimeSuspended, bool bReqUpperBoundsCheck)
{
    JIT_UpdateWriteBarrierState(GCHeapUtilities::IsServerHeap());
    return SWB_PASS;
}

#ifdef FEATURE_USE_SOFTWARE_WRITE_WATCH_FOR_GC_HEAP
int SwitchToWriteWatchBarrier(bool isRuntimeSuspended)
{
    JIT_UpdateWriteBarrierState(GCHeapUtilities::IsServerHeap());
    return SWB_PASS;
}

int SwitchToNonWriteWatchBarrier(bool isRuntimeSuspended)
{
    JIT_UpdateWriteBarrierState(GCHeapUtilities::IsServerHeap());
    return SWB_PASS;
}
#endif // FEATURE_USE_SOFTWARE_WRITE_WATCH_FOR_GC_HEAP
#endif // CROSSGEN_COMPILE

#ifdef DACCESS_COMPILE
BOOL GetAnyThunkTarget (T_CONTEXT *pctx, TADDR *pTarget, TADDR *pTargetMethodDesc)
{
    _ASSERTE(!"MIPS64:NYI");
    return FALSE;
}
#endif // DACCESS_COMPILE

#ifndef DACCESS_COMPILE
// ----------------------------------------------------------------
// StubLinkerCPU methods
// ----------------------------------------------------------------

#define R0 IntReg(0)
#define AT IntReg(1)
#define A0 IntReg(4)
#define A1 IntReg(5)
#define T1 IntReg(13)
#define T8 IntReg(24)
#define T9 IntReg(25)

void StubLinkerCPU::EmitJumpRegister(IntReg regTarget)
{
    // jr(regTarget)
    Emit32(((regTarget & 0x1f)<<21) | 0x08);
    EmitNop();
}

void StubLinkerCPU::EmitLoadStoreRegPairImm(DWORD flags, IntReg Rt1, IntReg Rt2, IntReg Rn, int offset)
{
    EmitLoadStoreRegPairImm(flags, (int)Rt1, (int)Rt2, Rn, offset, FALSE);
}

void StubLinkerCPU::EmitLoadStoreRegPairImm(DWORD flags, VecReg Vt1, VecReg Vt2, IntReg Rn, int offset)
{
    EmitLoadStoreRegPairImm(flags, (int)Vt1, (int)Vt2, Rn, offset, TRUE);
}

void StubLinkerCPU::EmitLoadStoreRegPairImm(DWORD flags, int regNum1, int regNum2, IntReg Rn, int offset, BOOL isVec)
{
    _ASSERTE(isVec == FALSE); // FIXME: VecReg not supported yet
    BOOL isLoad    = flags & 1;
    _ASSERTE((-512 <= offset) && (offset <= 504));
    _ASSERTE((offset & 7) == 0);
    if (isLoad) {
        // ld(regNum1, Rn, offset);
        Emit32(emitIns_O_R_R_I(0x37, Rn, regNum1, offset));
        // ld(regNum2, Rn, offset + 8);
        Emit32(emitIns_O_R_R_I(0x37, Rn, regNum2, offset + 8));
    } else {
        // sd(regNum1, Rn, offset);
        Emit32(emitIns_O_R_R_I(0x3f, Rn, regNum1, offset));
        // sd(regNum2, Rn, offset + 8);
        Emit32(emitIns_O_R_R_I(0x3f, Rn, regNum2, offset + 8));
    }
}

void StubLinkerCPU::EmitLoadStoreRegImm(DWORD flags, IntReg Rt, IntReg Rn, int offset)
{
    EmitLoadStoreRegImm(flags, (int)Rt, Rn, offset, FALSE);
}
void StubLinkerCPU::EmitLoadStoreRegImm(DWORD flags, VecReg Vt, IntReg Rn, int offset)
{
    EmitLoadStoreRegImm(flags, (int)Vt, Rn, offset, TRUE);
}

void StubLinkerCPU::EmitLoadStoreRegImm(DWORD flags, int regNum, IntReg Rn, int offset, BOOL isVec)
{
    _ASSERTE(isVec == FALSE); // FIXME: VecReg not supported yet
    BOOL isLoad    = flags & 1;
    if (isLoad) {
        // ld(regNum, Rn, offset);
        Emit32(emitIns_O_R_R_I(0x37, Rn, regNum, offset));
    } else {
        // sd(regNum, Rn, offset);
        Emit32(emitIns_O_R_R_I(0x3f, Rn, regNum, offset));
    }
}

void StubLinkerCPU::EmitLoadFloatRegImm(FloatReg ft, IntReg base, int offset)
{
    // ldc1 ft, (offset)base
    _ASSERTE(offset <= 32767 && offset >= -32768);
    Emit32(0xd4000000 | (base.reg << 21) | (ft.reg << 16) | offset);
}

void StubLinkerCPU::EmitMovReg(IntReg Rd, IntReg Rm)
{
    // dadd(Rd, Rm, R0);
    Emit32(emitIns_R_R_R_O(Rm, R0, Rd, 0x2c));
}

void StubLinkerCPU::EmitMovFloatReg(FloatReg Fd, FloatReg Fs)
{
    // mov.d fd, fs
    Emit32(0x46200006 | (Fd.reg << 6) | (Fs.reg << 11));
}

void StubLinkerCPU::EmitSubImm(IntReg Rd, IntReg Rn, unsigned int value)
{
    // daddiu(AT, R0, value);
    Emit32(emitIns_O_R_R_I(0x19, R0, AT, value));
    // dsubu(Rd, Rn, AT);
    Emit32(emitIns_R_R_R_O(Rn, AT, Rd, 0x2f));
}

void StubLinkerCPU::EmitAddImm(IntReg Rd, IntReg Rn, unsigned int value)
{
    // daddiu(Rd, Rn, value);
    Emit32(emitIns_O_R_R_I(0x19, Rn, Rd, value));
}

void StubLinkerCPU::Init()
{
    new (gConditionalBranchIF) ConditionalBranchInstructionFormat();
    new (gBranchIF) BranchInstructionFormat();
    new (gLoadFromLabelIF) LoadFromLabelInstructionFormat();
}

// Emits code to adjust arguments for static delegate target.
VOID StubLinkerCPU::EmitShuffleThunk(ShuffleEntry *pShuffleEntryArray)
{
    // On entry a0 holds the delegate instance. Look up the real target address stored in the MethodPtrAux
    // field and save it in t9. Tailcall to the target method after re-arranging the arguments
    // ld  t9, offsetof(DelegateObject, _methodPtrAux)(a0)
    EmitLoadStoreRegImm(eLOAD, T9, A0, DelegateObject::GetOffsetOfMethodPtrAux());
    //add  t8, a0, DelegateObject::GetOffsetOfMethodPtrAux() - load the indirection cell into t8 used by ResolveWorkerAsmStub
    EmitAddImm(T8, A0, DelegateObject::GetOffsetOfMethodPtrAux());

    for (ShuffleEntry* pEntry = pShuffleEntryArray; pEntry->srcofs != ShuffleEntry::SENTINEL; pEntry++)
    {
        if (pEntry->srcofs & ShuffleEntry::REGMASK)
        {
            // Source in register, destination in register

            // Both the srcofs and dstofs must be of the same kind of registers - float or general purpose.
            _ASSERTE((pEntry->dstofs & ShuffleEntry::FPREGMASK) == (pEntry->srcofs & ShuffleEntry::FPREGMASK));
            // If source is present in register then destination must also be a register
            _ASSERTE(pEntry->dstofs & ShuffleEntry::REGMASK);
            _ASSERTE((pEntry->dstofs & ShuffleEntry::OFSREGMASK) < 8);
            _ASSERTE((pEntry->srcofs & ShuffleEntry::OFSREGMASK) < 8);

            if (pEntry->srcofs & ShuffleEntry::FPREGMASK)
            {
                // 12 is the offset of FirstFloatArgReg to FirstFloatReg
                EmitMovFloatReg(FloatReg((pEntry->dstofs & ShuffleEntry::OFSREGMASK) + 12), FloatReg((pEntry->srcofs & ShuffleEntry::OFSREGMASK) + 12));
            }
            else
            {
                // 4 is the offset of FirstGenArgReg to FirstGenReg
                EmitMovReg(IntReg((pEntry->dstofs & ShuffleEntry::OFSMASK) + 4), IntReg((pEntry->srcofs & ShuffleEntry::OFSMASK) + 4));
            }
        }
        else if (pEntry->dstofs & ShuffleEntry::REGMASK)
        {
            // source must be on the stack
            _ASSERTE(!(pEntry->srcofs & ShuffleEntry::REGMASK));

            if (pEntry->dstofs & ShuffleEntry::FPREGMASK)
            {
                EmitLoadFloatRegImm(FloatReg((pEntry->dstofs & ShuffleEntry::OFSREGMASK) + 12), RegSp, pEntry->srcofs * sizeof(void*));
            }
            else
            {
                EmitLoadStoreRegImm(eLOAD, IntReg((pEntry->dstofs & ShuffleEntry::OFSMASK) + 4), RegSp, pEntry->srcofs * sizeof(void*));
            }
        }
        else
        {
            // source must be on the stack
            _ASSERTE(!(pEntry->srcofs & ShuffleEntry::REGMASK));

            // dest must be on the stack
            _ASSERTE(!(pEntry->dstofs & ShuffleEntry::REGMASK));

            EmitLoadStoreRegImm(eLOAD, AT, RegSp, pEntry->srcofs * sizeof(void*));
            EmitLoadStoreRegImm(eSTORE, AT, RegSp, pEntry->dstofs * sizeof(void*));
        }
    }

    // Tailcall to target
    // jr  t9
    EmitJumpRegister(T9);
}

void StubLinkerCPU::EmitCallLabel(CodeLabel *target, BOOL fTailCall, BOOL fIndirect)
{
    BranchInstructionFormat::VariationCodes variationCode = BranchInstructionFormat::VariationCodes::BIF_VAR_JUMP;
    if (!fTailCall)
        variationCode = static_cast<BranchInstructionFormat::VariationCodes>(variationCode | BranchInstructionFormat::VariationCodes::BIF_VAR_CALL);
    if (fIndirect)
        variationCode = static_cast<BranchInstructionFormat::VariationCodes>(variationCode | BranchInstructionFormat::VariationCodes::BIF_VAR_INDIRECT);

    EmitLabelRef(target, reinterpret_cast<BranchInstructionFormat&>(gBranchIF), (UINT)variationCode);

}

void StubLinkerCPU::EmitCallManagedMethod(MethodDesc *pMD, BOOL fTailCall)
{
    // Use direct call if possible.
    if (pMD->HasStableEntryPoint())
    {
        EmitCallLabel(NewExternalCodeLabel((LPVOID)pMD->GetStableEntryPoint()), fTailCall, FALSE);
    }
    else
    {
        EmitCallLabel(NewExternalCodeLabel((LPVOID)pMD->GetAddrOfSlot()), fTailCall, TRUE);
    }
}

#ifndef CROSSGEN_COMPILE

void StubLinkerCPU::EmitUnboxMethodStub(MethodDesc *pMD)
{
    _ASSERTE(!pMD->RequiresInstMethodDescArg());

    // Address of the value type is address of the boxed instance plus sizeof(MethodDesc*).
    //  daddiu  a0, a0, sizeof(MethodDesc*)
    EmitAddImm(A0, A0, sizeof(MethodDesc*));

    // Tail call the real target.
    EmitCallManagedMethod(pMD, TRUE /* tail call */);
}

#ifdef FEATURE_READYTORUN

//
// Allocation of dynamic helpers
//

#define DYNAMIC_HELPER_ALIGNMENT sizeof(TADDR)

#define BEGIN_DYNAMIC_HELPER_EMIT(size) \
    SIZE_T cb = size; \
    SIZE_T cbAligned = ALIGN_UP(cb, DYNAMIC_HELPER_ALIGNMENT); \
    BYTE * pStart = (BYTE *)(void *)pAllocator->GetDynamicHelpersHeap()->AllocAlignedMem(cbAligned, DYNAMIC_HELPER_ALIGNMENT); \
    BYTE * p = pStart;

#define END_DYNAMIC_HELPER_EMIT() \
    _ASSERTE(pStart + cb == p); \
    while (p < pStart + cbAligned) { *(DWORD*)p = 0xBADC0DF0; p += 4; }\
    ClrFlushInstructionCache(pStart, cbAligned); \
    return (PCODE)pStart

// Uses x8 as scratch register to store address of data label
// After load x8 is increment to point to next data
// only accepts positive offsets
static void LoadRegPair(BYTE* p, int reg1, int reg2, UINT32 offset)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    LIMITED_METHOD_CONTRACT;

    // adr x8, <label>
    *(DWORD*)(p + 0) = 0x10000008 | ((offset >> 2) << 5);
    // ldp reg1, reg2, [x8], #16 ; postindex & wback
    *(DWORD*)(p + 4) = 0xa8c10100 | (reg2 << 10) | reg1;
}

PCODE DynamicHelpers::CreateHelper(LoaderAllocator * pAllocator, TADDR arg, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(32);

    // adr x8, <label>
    // ldp x0, x12, [x8]
    LoadRegPair(p, 0, 12, 16);
    p += 8;
    // br x12
    *(DWORD*)p = 0xd61f0180;
    p += 4;

    // padding to make 8 byte aligned
    *(DWORD*)p = 0xBADC0DF0; p += 4;

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;
    // target
    *(PCODE*)p = target;
    p += 8;

    END_DYNAMIC_HELPER_EMIT();
}

// Caller must ensure sufficient byte are allocated including padding (if applicable)
void DynamicHelpers::EmitHelperWithArg(BYTE*& p, LoaderAllocator * pAllocator, TADDR arg, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    // if p is already aligned at 8-byte then padding is required for data alignment
    bool padding = (((uintptr_t)p & 0x7) == 0);

    // adr x8, <label>
    // ldp x1, x12, [x8]
    LoadRegPair(p, 1, 12, padding?16:12);
    p += 8;

    // br x12
    *(DWORD*)p = 0xd61f0180;
    p += 4;

    if(padding)
    {
        // padding to make 8 byte aligned
        *(DWORD*)p = 0xBADC0DF0;
        p += 4;
    }

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;
    // target
    *(PCODE*)p = target;
    p += 8;
}

PCODE DynamicHelpers::CreateHelperWithArg(LoaderAllocator * pAllocator, TADDR arg, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(32);

    EmitHelperWithArg(p, pAllocator, arg, target);

    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateHelper(LoaderAllocator * pAllocator, TADDR arg, TADDR arg2, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(40);

    // adr x8, <label>
    // ldp x0, x1, [x8] ; wback
    LoadRegPair(p, 0, 1, 16);
    p += 8;

    // ldr x12, [x8]
    *(DWORD*)p = 0xf940010c;
    p += 4;
    // br x12
    *(DWORD*)p = 0xd61f0180;
    p += 4;
    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;
    // arg2
    *(TADDR*)p = arg2;
    p += 8;
    // target
    *(TADDR*)p = target;
    p += 8;

    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateHelperArgMove(LoaderAllocator * pAllocator, TADDR arg, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(32);

    // mov x1, x0
    *(DWORD*)p = 0x91000001;
    p += 4;

    // adr x8, <label>
    // ldp x0, x12, [x8]
    LoadRegPair(p, 0, 12, 12);
    p += 8;

    // br x12
    *(DWORD*)p = 0xd61f0180;
    p += 4;

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;
    // target
    *(TADDR*)p = target;
    p += 8;

    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateReturn(LoaderAllocator * pAllocator)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(4);

    // br lr
    *(DWORD*)p = 0xd61f03c0;
    p += 4;
    END_DYNAMIC_HELPER_EMIT();
}

////FIXME for MIPS.
PCODE DynamicHelpers::CreateReturnConst(LoaderAllocator * pAllocator, TADDR arg)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(16);

    // ldr x0, <lable>
    *(DWORD*)p = 0x58000040;
    p += 4;

    // br lr
    *(DWORD*)p = 0xd61f03c0;
    p += 4;

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;

    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateReturnIndirConst(LoaderAllocator * pAllocator, TADDR arg, INT8 offset)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(24);

    // ldr x0, <label>
    *(DWORD*)p = 0x58000080;
    p += 4;

    // ldr x0, [x0]
    *(DWORD*)p = 0xf9400000;
    p += 4;

    // add x0, x0, offset
    *(DWORD*)p = 0x91000000 | (offset << 10);
    p += 4;

    // br lr
    *(DWORD*)p = 0xd61f03c0;
    p += 4;

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;

    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateHelperWithTwoArgs(LoaderAllocator * pAllocator, TADDR arg, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(32);

    // adr x8, <label>
    // ldp x2, x12, [x8]
    LoadRegPair(p, 2, 12, 16);
    p += 8;

    // br x12
    *(DWORD*)p = 0xd61f0180;
    p += 4;

    // padding to make 8 byte aligned
    *(DWORD*)p = 0xBADC0DF0; p += 4;

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;

    // target
    *(TADDR*)p = target;
    p += 8;
    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateHelperWithTwoArgs(LoaderAllocator * pAllocator, TADDR arg, TADDR arg2, PCODE target)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    BEGIN_DYNAMIC_HELPER_EMIT(40);

    // adr x8, <label>
    // ldp x2, x3, [x8]; wback
    LoadRegPair(p, 2, 3, 16);
    p += 8;

    // ldr x12, [x8]
    *(DWORD*)p = 0xf940010c;
    p += 4;

    // br x12
    *(DWORD*)p = 0xd61f0180;
    p += 4;

    // label:
    // arg
    *(TADDR*)p = arg;
    p += 8;
    // arg2
    *(TADDR*)p = arg2;
    p += 8;
    // target
    *(TADDR*)p = target;
    p += 8;
    END_DYNAMIC_HELPER_EMIT();
}

PCODE DynamicHelpers::CreateDictionaryLookupHelper(LoaderAllocator * pAllocator, CORINFO_RUNTIME_LOOKUP * pLookup, DWORD dictionaryIndexAndSlot, Module * pModule)
{
    _ASSERTE(!"MIPS64: not implementation on mips64!!!");
    STANDARD_VM_CONTRACT;

    PCODE helperAddress = (pLookup->helper == CORINFO_HELP_RUNTIMEHANDLE_METHOD ?
        GetEEFuncEntryPoint(JIT_GenericHandleMethodWithSlotAndModule) :
        GetEEFuncEntryPoint(JIT_GenericHandleClassWithSlotAndModule));

    GenericHandleArgs * pArgs = (GenericHandleArgs *)(void *)pAllocator->GetDynamicHelpersHeap()->AllocAlignedMem(sizeof(GenericHandleArgs), DYNAMIC_HELPER_ALIGNMENT);
    pArgs->dictionaryIndexAndSlot = dictionaryIndexAndSlot;
    pArgs->signature = pLookup->signature;
    pArgs->module = (CORINFO_MODULE_HANDLE)pModule;

    // It's available only via the run-time helper function
    if (pLookup->indirections == CORINFO_USEHELPER)
    {
        BEGIN_DYNAMIC_HELPER_EMIT(32);

        // X0 already contains generic context parameter
        // reuse EmitHelperWithArg for below two operations
        // X1 <- pArgs
        // branch to helperAddress
        EmitHelperWithArg(p, pAllocator, (TADDR)pArgs, helperAddress);

        END_DYNAMIC_HELPER_EMIT();
    }
    else
    {
        int indirectionsCodeSize = 0;
        int indirectionsDataSize = 0;
        for (WORD i = 0; i < pLookup->indirections; i++) {
            indirectionsCodeSize += (pLookup->offsets[i] > 32760 ? 8 : 4); // if( > 32760) (8 code bytes) else 4 bytes for instruction with offset encoded in instruction
            indirectionsDataSize += (pLookup->offsets[i] > 32760 ? 4 : 0); // 4 bytes for storing indirection offset values
        }

        int codeSize = indirectionsCodeSize;
        if(pLookup->testForNull)
        {
            codeSize += 4; // mov
            codeSize += 12; // cbz-ret-mov
            //padding for 8-byte align (required by EmitHelperWithArg)
            if((codeSize & 0x7) == 0)
                codeSize += 4;
            codeSize += 28; // size of EmitHelperWithArg
        }
        else
        {
            codeSize += 4 ; /* ret */
        }

        codeSize += indirectionsDataSize;

        BEGIN_DYNAMIC_HELPER_EMIT(codeSize);

        if (pLookup->testForNull)
        {
            // mov x9, x0
            *(DWORD*)p = 0x91000009;
            p += 4;
        }

        // moving offset value wrt PC. Currently points to first indirection offset data.
        uint dataOffset = codeSize - indirectionsDataSize - (pLookup->testForNull ? 4 : 0);
        for (WORD i = 0; i < pLookup->indirections; i++)
        {
            if(pLookup->offsets[i] > 32760)
            {
                // ldr w10, [PC, #dataOffset]
                *(DWORD*)p = 0x1800000a | ((dataOffset>>2)<<5);
                p += 4;
                // ldr x0, [x0, x10]
                *(DWORD*)p = 0xf86a6800;
                p += 4;

                // move to next indirection offset data
                dataOffset = dataOffset - 8 + 4; // subtract 8 as we have moved PC by 8 and add 4 as next data is at 4 bytes from previous data
            }
            else
            {
                // offset must be 8 byte aligned
                _ASSERTE((pLookup->offsets[i] & 0x7) == 0);

                // ldr x0, [x0, #(pLookup->offsets[i])]
                *(DWORD*)p = 0xf9400000 | ( ((UINT32)pLookup->offsets[i]>>3) <<10 );
                p += 4;
                dataOffset -= 4; // subtract 4 as we have moved PC by 4
            }
        }

        // No null test required
        if (!pLookup->testForNull)
        {
            // ret lr
            *(DWORD*)p = 0xd65f03c0;
            p += 4;
        }
        else
        {
            // cbz x0, nullvaluelabel
            *(DWORD*)p = 0xb4000040;
            p += 4;
            // ret lr
            *(DWORD*)p = 0xd65f03c0;
            p += 4;
            // nullvaluelabel:
            // mov x0, x9
            *(DWORD*)p = 0x91000120;
            p += 4;
            // reuse EmitHelperWithArg for below two operations
            // X1 <- pArgs
            // branch to helperAddress
            EmitHelperWithArg(p, pAllocator, (TADDR)pArgs, helperAddress);
        }

        // datalabel:
        for (WORD i = 0; i < pLookup->indirections; i++)
        {
            if(pLookup->offsets[i] > 32760)
            {
                _ASSERTE((pLookup->offsets[i] & 0xffffffff00000000) == 0);
                *(UINT32*)p = (UINT32)pLookup->offsets[i];
                p += 4;
            }
        }

        END_DYNAMIC_HELPER_EMIT();
    }
}
#endif // FEATURE_READYTORUN

#endif // CROSSGEN_COMPILE

#endif // #ifndef DACCESS_COMPILE
