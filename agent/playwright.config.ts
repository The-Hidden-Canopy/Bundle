import { defineConfig } from '@playwright/test';

const uiURL = process.env.POCKETFUL_UI_URL ?? 'http://localhost:3000';
const apiURL = process.env.POCKETFUL_API_URL ?? 'http://localhost:8080';
process.env.POCKETFUL_API_URL ??= apiURL;

export default defineConfig({
  testDir: 'tests/pocketful',
  timeout: 45_000,
  expect: { timeout: 7_500 },
  fullyParallel: false,
  workers: 1,
  retries: process.env.CI ? 1 : 0,
  forbidOnly: !!process.env.CI,
  outputDir: 'test-results/pocketful',
  reporter: [['line'], ['html', { open: 'never' }]],
  metadata: { apiURL },
  use: {
    baseURL: uiURL,
    actionTimeout: 10_000,
    navigationTimeout: 15_000,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'retain-on-failure',
  },
});
