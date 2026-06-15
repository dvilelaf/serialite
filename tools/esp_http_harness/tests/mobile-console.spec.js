const { test, expect } = require('@playwright/test');

test('mobile operator opens xterm console and terminal controls work end-to-end', async ({ page }) => {
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
  await expect(page).toHaveURL(/\/terminal$/);
  await expect(page.getByPlaceholder('Web password')).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Open console' })).toHaveCount(0);

  await expect(page.locator('#state')).toBeVisible();
  await expect(page.locator('.xterm')).toBeVisible();
  await expect(page.locator('#terminal')).toContainText(/Serial console ready|harness login:/);
  await expect(page.locator('#input')).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Send' })).toHaveCount(0);
  await expect(page.locator('#state')).toContainText('OK');
  await expect(page.locator('#state')).toHaveAttribute('title', 'Stream OK');
  await expect(page.locator('#streamDot')).toHaveCSS('border-radius', '50%');
  await page.locator('#consoleNotice').evaluate((el) => el.classList.remove('hidden'));
  await expect(page.locator('#consoleNotice')).toHaveCSS('transform', /matrix\(1, 0, 0, 1, -/);
  await page.locator('#consoleNotice').evaluate((el) => el.classList.add('hidden'));
  await expect(page.getByRole('button', { name: 'Fullscreen' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'More controls' })).toHaveText('+');
  await page.getByRole('button', { name: 'More controls' }).click();
  await expect(page.locator('#keys')).toHaveClass(/open/);
  await expect(page.getByRole('button', { name: 'Rotate WiFi' })).toBeVisible();
  await page.locator('#keys').dispatchEvent('mouseleave');
  await expect(page.locator('#keys')).not.toHaveClass(/open/);
  await page.getByRole('button', { name: 'More controls' }).click();
  await page.getByRole('button', { name: 'Ctrl+C' }).click();
  await expect(page.locator('#terminal')).not.toContainText('\\u0003');
  await page.locator('#terminal').click();
  await page.keyboard.type('whoami');
  await page.keyboard.press('Enter');
  await expect(page.locator('#terminal')).toContainText('whoami');
  await page.keyboard.press('Control+L');

  expect(consoleErrors).toEqual([]);
  expect(pageErrors).toEqual([]);
  expect(failedRequests).toEqual([]);

  page.on('dialog', (dialog) => dialog.accept());
  await page.getByRole('button', { name: 'More controls' }).click();
  await page.getByRole('button', { name: 'Emergency lock' }).click();
  await expect
    .poll(async () => {
      const response = await page.request.get('/terminal-status.json');
      return response.status();
    })
    .toBe(401);

  expect(pageErrors).toEqual([]);
  expect(failedRequests).toEqual([]);
});
