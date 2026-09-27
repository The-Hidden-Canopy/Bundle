// tests/pocketful/test.base.ts
import { test as base, type Page } from '@playwright/test';

interface PocketfulFixture {
  reset: (users: Array<{
    id: string;
    email: string;
    password: string;
    display_name: string;
    handle: string;
    balance: number;
  }>) => Promise<void>;
}

export const test = base.extend<{ pocketful: PocketfulFixture }>({
  pocketful: async ({ page }, use) => {
    await use({
      reset: async (users) => {
        const resp = await page.request.post('http://localhost:8080/_test/reset', {
          data: { currency: 'USD', minor_units: 2, users },
        });
        if (!resp.ok()) {
          throw new Error(`Reset failed: ${resp.status()} ${await resp.text()}`);
        }
      },
    });
  },
});

export { expect } from '@playwright/test';
