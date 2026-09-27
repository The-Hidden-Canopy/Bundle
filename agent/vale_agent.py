"""Vale the interface engineer agent (Claude SDK)."""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path

from dotenv import load_dotenv

from band import Agent, configure_logging
from band.adapters import ClaudeSDKAdapter
from band.core.types import Emit

configure_logging(
    logging.INFO,
    extra_loggers={
        "band_claude_sdk_agent": logging.INFO,
        "session_manager": logging.INFO,
    },
)
logger = logging.getLogger(__name__)

VALE_SYSTEM_PROMPT = """\
You are Vale, an Interface Engineer.

You build user-facing interfaces, interaction flows, client state, \
accessibility, and service integrations. You ensure the interface \
accurately represents underlying application state, handle loading and \
failure conditions clearly, and provide tested, reproducible user behavior.

Focus areas: frontend, UI, and testing.

IMPORTANT: The room cannot see plain text you return. You must call the
`band_send_message` tool to actually deliver a reply. Finishing a turn
without calling `band_send_message` means your response is silently
dropped and nobody in the room sees it. If you have something to say,
call `band_send_message` with that content before ending your turn.
"""


async def main() -> None:
    """Run Vale the interface engineer agent."""
    load_dotenv()

    adapter = ClaudeSDKAdapter(
        custom_section=VALE_SYSTEM_PROMPT,
        effort="high",
        # Full bypass: Vale runs headless with nobody to click an approval
        # prompt, so anything short of bypassPermissions leaves Bash (git
        # add/commit/push, etc.) permanently stuck waiting for a click that
        # never comes. Chosen explicitly - this lets Vale commit and push
        # to the shared repo with zero human review per action.
        permission_mode="bypassPermissions",
        cwd=str(Path(__file__).resolve().parent.parent),
        emit=Emit.TOOL_CALLS | Emit.THOUGHTS,
    )

    agent = Agent.from_config(
        "vale_agent",
        adapter=adapter,
    )

    logger.info("Vale is online.")
    logger.info("Agent ID: %s", agent.runtime.agent_id)
    logger.info("Press Ctrl+C to stop")

    try:
        async with agent:
            await agent.run_forever()
    except KeyboardInterrupt:
        logger.info("Shutting down...")


if __name__ == "__main__":
    asyncio.run(main())
