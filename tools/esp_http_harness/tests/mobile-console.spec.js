const { test, expect } = require('@playwright/test');

test('mobile operator auth and terminal controls work end-to-end', async ({ page }) => {
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
    const failure = request.failure();
    const url = request.url();
    if (!url.endsWith('/favicon.ico')) {
      failedRequests.push(`${request.method()} ${url}: ${failure && failure.errorText}`);
    }
  });

  await page.goto('/login');
  await expect(page.getByRole('heading', { name: 'Serial console' })).toBeVisible();

  await page.getByPlaceholder('Web password').fill('wrong password');
  await page.getByRole('button', { name: 'Unlock console' }).click();
  await expect(page.locator('body')).toContainText('invalid credentials');
  consoleErrors.length = 0;
  failedRequests.length = 0;

  await page.goto('/login');
  await page.getByPlaceholder('Web password').fill('alphazoom');
  await page.getByRole('button', { name: 'Unlock console' }).click();

  await expect(page).toHaveURL(/\/terminal$/);
  await expect(page.locator('#state')).toBeVisible();
  await expect(page.locator('#terminal')).toContainText(/Control active|harness login:/);
  await expect(page.locator('#input')).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Send' })).toHaveCount(0);
  await expect(page.locator('#state')).toContainText('STREAM OK');
  await page.locator('#terminal').focus();
  await page.keyboard.type('whoami');
  await page.keyboard.press('Enter');
  await expect(page.locator('#terminal')).toContainText('whoami');

  page.on('dialog', (dialog) => dialog.accept());
  await page.getByRole('button', { name: 'More' }).click();
  await page.getByRole('button', { name: 'Emergency lock' }).click();
  await expect
    .poll(async () => {
      const response = await page.request.get('/terminal-status.json');
      return response.status();
    })
    .toBe(401);

  expect(consoleErrors).toEqual([]);
  expect(pageErrors).toEqual([]);
  expect(failedRequests).toEqual([]);
});
