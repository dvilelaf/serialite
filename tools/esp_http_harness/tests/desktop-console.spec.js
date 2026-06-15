const { test, expect } = require('@playwright/test');

test.use({
  viewport: { width: 1440, height: 900 },
  isMobile: false,
  hasTouch: false,
  userAgent:
    'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36',
});

test('desktop operator gets a terminal-first console without mobile regressions', async ({ page }) => {
  const consoleErrors = [];
  const pageErrors = [];
  const failedRequests = [];

  page.on('console', (msg) => {
    if (msg.type() === 'error') {
      consoleErrors.push(msg.text());
    }
  });
  page.on('pageerror', (error) => pageErrors.push(error.message));
  page.on('requestfailed', (request) => {
    if (!request.url().endsWith('/favicon.ico')) {
      const failure = request.failure();
      failedRequests.push(`${request.method()} ${request.url()}: ${failure && failure.errorText}`);
    }
  });

  await page.goto('/terminal');
  await expect(page.locator('.xterm')).toBeVisible();
  await expect(page.locator('#terminal')).toContainText(/Serial console ready|harness login:/);
  await expect(page.locator('#hud')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Fullscreen' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'More controls' })).toHaveText('+');

  await page.getByRole('button', { name: 'More controls' }).click();
  await expect(page.locator('#keys')).toHaveClass(/open/);
  await expect(page.getByRole('button', { name: 'Close controls' })).toBeVisible();
  await page.getByRole('button', { name: 'Close controls' }).click();
  await expect(page.locator('#keys')).not.toHaveClass(/open/);

  await page.locator('#terminal').click();
  await page.keyboard.type('desktop-check');
  await page.keyboard.press('Enter');
  await expect(page.locator('#terminal')).toContainText('desktop-check');

  expect(consoleErrors).toEqual([]);
  expect(pageErrors).toEqual([]);
  expect(failedRequests).toEqual([]);
});
