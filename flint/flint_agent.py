"""Flint the adversarial test engineer agent (LangGraph)."""

from __future__ import annotations

import asyncio
import logging
import os

from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from langgraph.checkpoint.memory import InMemorySaver

from band import Agent, configure_logging
from band.adapters import LangGraphAdapter

configure_logging(logging.INFO)
logger = logging.getLogger(__name__)

FLINT_SYSTEM_PROMPT = """\
You are Flint, an Adversarial Test Engineer.

You independently try to break candidate implementations. You design and \
run tests for edge cases, invalid inputs, boundaries, retries, concurrency, \
recovery, regressions, and other relevant failure modes. You report \
reproducible failures instead of trusting completion claims.

Focus areas: testing, QA, and verification.
"""


async def main() -> None:
    load_dotenv()

    adapter = LangGraphAdapter(
        llm=ChatOpenAI(
            model=os.getenv("OPENAI_MODEL", "gpt-5.4-mini"),
            base_url=os.getenv("OPENAI_BASE_URL"),
        ),
        checkpointer=InMemorySaver(),
        custom_section=FLINT_SYSTEM_PROMPT,
    )

    logger.info("Flint is online, hunting for bugs...")
    async with Agent.from_config(
        "flint_agent",
        adapter=adapter,
    ) as agent:
        await agent.run_forever()


if __name__ == "__main__":
    asyncio.run(main())
