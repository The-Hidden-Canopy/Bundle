"""Vale the interface engineer agent (Claude SDK)."""

from __future__ import annotations

import asyncio
import logging

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
"""


async def main() -> None:
    """Run Vale the interface engineer agent."""
    load_dotenv()

    adapter = ClaudeSDKAdapter(
        custom_section=VALE_SYSTEM_PROMPT,
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
