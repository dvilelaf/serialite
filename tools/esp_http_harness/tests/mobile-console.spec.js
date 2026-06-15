const { test, expect } = require('@playwright/test');

test('mobile operator can log in and reach terminal without browser errors', async ({ page }) => {
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

  await page.goto('/');
  await expect(page).toHaveURL(/\/login$/);
  await expect(page.getByRole('heading', { name: 'Serial console' })).toBeVisible();

  await page.getByPlaceholder('Web password').fill('alpha zoom');
  await page.getByRole('button', { name: 'Unlock console' }).click();

  await expect(page).toHaveURL(/\/terminal$/);
  await expect(page.locator('#mode')).toBeVisible();
  await expect(page.locator('#state')).toBeVisible();
  await expect(page.locator('#term')).toContainText(/Waiting for serial output|harness login:/);
  await expect(page.locator('#input')).toBeDisabled();

  await page.waitForTimeout(1000);

  expect(consoleErrors).toEqual([]);
  expect(pageErrors).toEqual([]);
  expect(failedRequests).toEqual([]);
});
