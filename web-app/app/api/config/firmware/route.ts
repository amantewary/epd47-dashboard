import { NextRequest, NextResponse } from 'next/server';
import path from 'path';
import { readFile, writeFile } from 'fs/promises';
import { AppConfig } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

const DEFAULT_SETTINGS = {
  ntpServer: 'pool.ntp.org',
  gmtOffsetSec: '-18000',
  daylightOffsetSec: '3600',
  haUseHttps: '0',
  useFahrenheit: '0',
  use24hTime: '0',
  sleepIntervalMinutes: '60',
};

function cString(value: string): string {
  return value.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function extractDefine(source: string, name: string, fallback: string): string {
  const match = source.match(new RegExp(`^#define\\s+${name}\\s+(.+)$`, 'm'));
  return match?.[1]?.trim() || fallback;
}

function normalizeEntityList(value: unknown): string[] {
  return Array.isArray(value)
    ? value
        .map((item) => String(item).trim())
        .filter(Boolean)
        .slice(0, 10)
    : [];
}

function renderEntityDefines(prefix: string, entities: string[]): string {
  return [
    `#define ENTITY_${prefix}_COUNT ${entities.length}`,
    ...entities.map((entity, index) => `#define ENTITY_${prefix.slice(0, -1)}_${index + 1} "${cString(entity)}"`),
  ].join('\n');
}

function renderConfig(config: AppConfig, existingConfig: string): string {
  const todoEntities = normalizeEntityList(config.todoEntities);
  const calendarEntities = normalizeEntityList(config.calendarEntities);
  const ntpServer = extractDefine(existingConfig, 'NTP_SERVER', `"${DEFAULT_SETTINGS.ntpServer}"`);
  const gmtOffsetSec = extractDefine(existingConfig, 'GMT_OFFSET_SEC', DEFAULT_SETTINGS.gmtOffsetSec);
  const daylightOffsetSec = extractDefine(existingConfig, 'DAYLIGHT_OFFSET_SEC', DEFAULT_SETTINGS.daylightOffsetSec);
  const haUseHttps = extractDefine(existingConfig, 'HA_USE_HTTPS', DEFAULT_SETTINGS.haUseHttps);
  const useFahrenheit = extractDefine(existingConfig, 'USE_FAHRENHEIT', DEFAULT_SETTINGS.useFahrenheit);
  const use24hTime = extractDefine(existingConfig, 'USE_24H_TIME', DEFAULT_SETTINGS.use24hTime);
  const sleepIntervalMinutes = extractDefine(existingConfig, 'SLEEP_INTERVAL_MINUTES', DEFAULT_SETTINGS.sleepIntervalMinutes);

  return `#pragma once

// Home Assistant Entity Configuration
// This file is generated from the local web app settings and is not tracked by git.

// Weather entity
#define ENTITY_WEATHER "${cString(config.weatherEntity || '')}"

// Quote sensor (optional)
#define ENTITY_QUOTE "${cString(config.quoteEntity || '')}"

// Todo entities
${renderEntityDefines('TODOS', todoEntities)}

// Calendar entities
${renderEntityDefines('CALENDARS', calendarEntities)}

// NTP Timezone Configuration
#define NTP_SERVER ${ntpServer}
#define GMT_OFFSET_SEC ${gmtOffsetSec}
#define DAYLIGHT_OFFSET_SEC ${daylightOffsetSec}

// Home Assistant connection
#define HA_USE_HTTPS ${haUseHttps}

// Display preferences
#define USE_FAHRENHEIT ${useFahrenheit}
#define USE_24H_TIME ${use24hTime}

// Deep sleep interval (minutes) when running in battery-optimized mode
#define SLEEP_INTERVAL_MINUTES ${sleepIntervalMinutes}
`;
}

function resolveProjectRoot(): string {
  const configuredRoot = process.env.PIO_PROJECT_ROOT;
  const fallbackRoot = path.resolve(process.cwd(), '..');

  if (!configuredRoot) {
    return fallbackRoot;
  }

  return configuredRoot;
}

export async function POST(request: NextRequest) {
  const projectRoot = resolveProjectRoot();

  const config = await request.json().catch(() => null) as AppConfig | null;
  if (!config || !config.weatherEntity || !config.quoteEntity) {
    return NextResponse.json(
      { error: 'Missing required firmware entities.' },
      { status: 400 }
    );
  }

  const configPath = path.join(projectRoot, 'examples', 'ha_calendar', 'config.h');
  const existingConfig = await readFile(configPath, 'utf8').catch(() => '');
  await writeFile(configPath, renderConfig(config, existingConfig), 'utf8');

  return NextResponse.json({
    success: true,
    path: configPath,
  });
}
