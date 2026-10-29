#pragma once

#include <cli/cli_vcp.h>

#ifdef SUBGHZ_GARAGE_HAS_CLI_VCP_LOCK

static inline void subghz_garage_cli_vcp_session_lock(CliVcp* cli_vcp) {
    cli_vcp_session_lock(cli_vcp);
}

static inline void subghz_garage_cli_vcp_session_unlock(CliVcp* cli_vcp) {
    cli_vcp_session_unlock(cli_vcp);
}

#else

static inline void subghz_garage_cli_vcp_session_lock(CliVcp* cli_vcp) {
    UNUSED(cli_vcp);
}

static inline void subghz_garage_cli_vcp_session_unlock(CliVcp* cli_vcp) {
    UNUSED(cli_vcp);
}

#endif
