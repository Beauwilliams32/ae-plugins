// SpectraKeysPiPL.r — PiPL resource for the SpectraKeys effect.
//
// Reconstructed from the RECOVERED spec (specs/SpectraKeys.json), not copied from
// any surviving binary. Building it requires a local Adobe After Effects SDK:
// AE_SDK_DIR must point at the SDK's "Examples" directory.
//
//   Rez -useDF -o SpectraKeys.rsrc \
//       -I "$AE_SDK_DIR/Headers" -I "$AE_SDK_DIR/Resources" \
//       -d AE_OS_MAC -d AE_PROC_ARM64 SpectraKeysPiPL.r
//
// Everything the PiPL asserts (kind, name, category, bundle code entry points,
// spec version, effect version, global flags, match name) was read out of the
// surviving bundle's PIPL resource with `DeRez -useDF` and cross-checked against
// specs/SpectraKeys.json, which tools/recover.mjs derives independently.

#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
	{	/* array properties: 13 elements */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"SpectraKeys"
		},
		/* [3] */
		Category {
			"Williams Media"
		},
#if defined(AE_OS_MAC)
		/* [4] CodeMacIntel64 -> 'mi64' */
		CodeMacIntel64 {"EffectMain"},
		/* [5] CodeMacARM64   -> 'ma64' */
		CodeMacARM64 {"EffectMain"},
#elif defined(AE_OS_WIN)
	#if defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
	#elif defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
	#endif
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			0x00080001
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
			0x06008064
		},
		AE_Effect_Global_OutFlags_2 {
			0x08000008
		},
		/* [11] */
		AE_Effect_Match_Name {
			"WMH SpectraKeys"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		}
	}
};
