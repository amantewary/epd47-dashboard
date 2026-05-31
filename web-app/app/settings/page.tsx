'use client';

import { useState, useEffect } from 'react';
import { useRouter } from 'next/navigation';
import { useForm } from 'react-hook-form';
import { AppConfig } from '@/lib/types';
import { DEFAULT_CONFIG, mergeWithDefaultConfig } from '@/lib/default-config';

export default function SettingsPage() {
  const router = useRouter();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState(false);
  const [savedConfig, setSavedConfig] = useState<AppConfig | null>(null);
  const [availableEntities, setAvailableEntities] = useState<{
    todos: string[];
    calendars: string[];
    weather: string[];
    sensors: string[];
  }>({
    todos: [],
    calendars: [],
    weather: [],
    sensors: [],
  });

  const { register, handleSubmit, watch, setValue, reset, formState: { errors } } = useForm<AppConfig>({
    defaultValues: DEFAULT_CONFIG,
  });

  const watchedHost = watch('host');
  const watchedPort = watch('port');
  const watchedToken = watch('token');
  const watchedTodoEntities = watch('todoEntities') || [];
  const watchedCalendarEntities = watch('calendarEntities') || [];

  // Load saved config
  useEffect(() => {
    const savedConfig = localStorage.getItem('epd47-config');
    if (savedConfig) {
      try {
        const parsed = JSON.parse(savedConfig);
        const merged = mergeWithDefaultConfig(parsed);
        setSavedConfig(merged);
        reset(merged);
        // Seed available entity lists with saved selections so dropdowns keep context
        setAvailableEntities((prev) => ({
          todos: merged.todoEntities.length
            ? Array.from(new Set([...merged.todoEntities, ...prev.todos]))
            : prev.todos,
          calendars: merged.calendarEntities.length
            ? Array.from(new Set([...merged.calendarEntities, ...prev.calendars]))
            : prev.calendars,
          weather: parsed.weatherEntity
            ? Array.from(new Set([parsed.weatherEntity, ...prev.weather]))
            : prev.weather,
          sensors: parsed.quoteEntity
            ? Array.from(new Set([parsed.quoteEntity, ...prev.sensors]))
            : prev.sensors,
        }));
      } catch (e) {
        console.error('Failed to load saved config:', e);
      }
    } else {
      reset(DEFAULT_CONFIG);
      setSavedConfig(DEFAULT_CONFIG);
      setAvailableEntities((prev) => ({
        todos: DEFAULT_CONFIG.todoEntities.length
          ? Array.from(new Set([...DEFAULT_CONFIG.todoEntities, ...prev.todos]))
          : prev.todos,
        calendars: DEFAULT_CONFIG.calendarEntities.length
          ? Array.from(new Set([...DEFAULT_CONFIG.calendarEntities, ...prev.calendars]))
          : prev.calendars,
        weather: DEFAULT_CONFIG.weatherEntity
          ? Array.from(new Set([DEFAULT_CONFIG.weatherEntity, ...prev.weather]))
          : prev.weather,
        sensors: DEFAULT_CONFIG.quoteEntity
          ? Array.from(new Set([DEFAULT_CONFIG.quoteEntity, ...prev.sensors]))
          : prev.sensors,
      }));
    }
  }, [reset]);

  // Fetch available entities when HA config is provided
  useEffect(() => {
    if (watchedHost && watchedToken) {
      // Add a small delay to debounce rapid changes
      const timeoutId = setTimeout(() => {
        fetchAvailableEntities();
      }, 500);
      return () => clearTimeout(timeoutId);
    } else {
      // Clear entities if config is incomplete
      setAvailableEntities({ todos: [], calendars: [], weather: [], sensors: [] });
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [watchedHost, watchedPort, watchedToken]);

  const fetchAvailableEntities = async () => {
    if (!watchedHost || !watchedToken) {
      return;
    }

    try {
      setError(null);
      
      const response = await fetch('/api/ha/entities', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          host: watchedHost,
          port: watchedPort,
          token: watchedToken,
        }),
      });
      
      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.error || `Failed to fetch entities: ${response.status}`);
      }

      const data = await response.json();
      const mergedTodos = data.todos || [];
      const mergedCalendars = data.calendars || [];
      const mergedWeather = data.weather || [];
      const mergedSensors = data.sensors || [];

      // Keep saved/current selections in the list even if HA response is missing them
      for (const entity of savedConfig?.todoEntities || []) {
        if (!mergedTodos.includes(entity)) mergedTodos.unshift(entity);
      }
      for (const entity of watchedTodoEntities) {
        if (!mergedTodos.includes(entity)) mergedTodos.unshift(entity);
      }
      for (const entity of savedConfig?.calendarEntities || []) {
        if (!mergedCalendars.includes(entity)) mergedCalendars.unshift(entity);
      }
      for (const entity of watchedCalendarEntities) {
        if (!mergedCalendars.includes(entity)) mergedCalendars.unshift(entity);
      }

      // Keep saved selections in the list even if HA response is missing them
      if (savedConfig?.weatherEntity && !mergedWeather.includes(savedConfig.weatherEntity)) {
        mergedWeather.unshift(savedConfig.weatherEntity);
      }
      if (savedConfig?.quoteEntity && !mergedSensors.includes(savedConfig.quoteEntity)) {
        mergedSensors.unshift(savedConfig.quoteEntity);
      }

      setAvailableEntities({
        todos: mergedTodos,
        calendars: mergedCalendars,
        weather: mergedWeather,
        sensors: mergedSensors,
      });
    } catch (err: any) {
      console.error('Failed to fetch entities:', err);
      setError(`Failed to fetch entities: ${err.message}. Please check your Home Assistant connection and token.`);
      setAvailableEntities({
        todos: Array.from(new Set([...(savedConfig?.todoEntities || []), ...watchedTodoEntities])),
        calendars: Array.from(new Set([...(savedConfig?.calendarEntities || []), ...watchedCalendarEntities])),
        weather: savedConfig?.weatherEntity ? [savedConfig.weatherEntity] : [],
        sensors: savedConfig?.quoteEntity ? [savedConfig.quoteEntity] : [],
      });
    }
  };

  const onSubmit = async (data: AppConfig) => {
    setLoading(true);
    setError(null);
    setSuccess(false);

    try {
      // Validate configuration
      if (!data.host || !data.token) {
        throw new Error('Host and token are required');
      }

      if (!data.weatherEntity || !data.quoteEntity) {
        throw new Error('Weather and Quote entities are required');
      }

      // Ensure arrays are properly formatted
      const configToSave: AppConfig = {
        ...data,
        todoEntities: Array.isArray(data.todoEntities) ? data.todoEntities : [],
        calendarEntities: Array.isArray(data.calendarEntities) ? data.calendarEntities : [],
      };

      const firmwareResponse = await fetch('/api/config/firmware', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(configToSave),
      });

      if (!firmwareResponse.ok) {
        const errorData = await firmwareResponse.json().catch(() => ({}));
        throw new Error(errorData.error || 'Failed to sync firmware config');
      }

      localStorage.setItem('epd47-config', JSON.stringify(configToSave));
      setSuccess(true);
    } catch (err: any) {
      setError(err.message || 'Failed to save configuration');
      console.error('Settings save error:', err);
    } finally {
      setLoading(false);
    }
  };

  // Redirect when save succeeds
  useEffect(() => {
    if (!success) return;
    const timer = setTimeout(() => {
      router.push('/');
      window.location.reload();
    }, 1500);
    return () => clearTimeout(timer);
  }, [success, router]);

  return (
    <div className="min-h-screen bg-gray-100 p-8">
      <div className="max-w-4xl mx-auto bg-white rounded-lg shadow-lg p-8">
        <h1 className="text-3xl font-bold mb-6 text-epd-black">EPD47 Dashboard Settings</h1>

        {error && (
          <div className="mb-4 p-4 bg-red-100 border border-red-400 text-red-700 rounded">
            {error}
          </div>
        )}

        {success && (
          <div className="mb-4 p-4 bg-green-100 border border-green-400 text-green-700 rounded flex items-center justify-between gap-4">
            <span>Configuration saved successfully! Redirecting...</span>
            <button
              type="button"
              onClick={() => {
                router.push('/');
                window.location.reload();
              }}
              className="px-3 py-1 bg-epd-black text-white rounded hover:bg-epd-gray text-sm"
            >
              Go to Dashboard
            </button>
          </div>
        )}

        <form onSubmit={handleSubmit(onSubmit)} className="space-y-6">
          {/* Home Assistant Configuration */}
          <div className="border-b pb-6">
            <h2 className="text-xl font-semibold mb-4 text-epd-black">Home Assistant Configuration</h2>
            
            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="block text-sm font-medium mb-2 text-epd-black">
                  HA Host/IP
                </label>
                <input
                  type="text"
                  {...register('host', { required: true })}
                  className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black"
                  placeholder="192.168.1.100 or homeassistant.local"
                />
                {errors.host && (
                  <span className="text-red-500 text-sm">Host is required</span>
                )}
              </div>

              <div>
                <label className="block text-sm font-medium mb-2 text-epd-black">
                  HA Port
                </label>
                <input
                  type="number"
                  {...register('port', { required: true, valueAsNumber: true })}
                  className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black"
                  placeholder="8123"
                />
                {errors.port && (
                  <span className="text-red-500 text-sm">Port is required</span>
                )}
              </div>
            </div>

            <div className="mt-4">
              <label className="block text-sm font-medium mb-2 text-epd-black">
                Long-lived Access Token
              </label>
              <input
                type="password"
                {...register('token', { required: true })}
                className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black"
                placeholder="Enter your HA token"
              />
              {errors.token && (
                <span className="text-red-500 text-sm">Token is required</span>
              )}
            </div>
          </div>

          {/* Entity Selection */}
          <div className="border-b pb-6">
            <h2 className="text-xl font-semibold mb-4 text-epd-black">Entity Selection</h2>

            <div className="mb-4">
              <label className="block text-sm font-medium mb-2 text-epd-black">
                Weather Entity
                {!watchedHost || !watchedToken ? (
                  <span className="text-xs text-gray-500 ml-2">(Enter HA host and token first)</span>
                ) : availableEntities.weather.length === 0 ? (
                  <span className="text-xs text-orange-500 ml-2">(No weather entities found)</span>
                ) : null}
              </label>
              <select
                {...register('weatherEntity', { required: true })}
                disabled={!watchedHost || !watchedToken || availableEntities.weather.length === 0}
                className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black disabled:bg-gray-100 disabled:cursor-not-allowed"
              >
                <option value="">
                  {!watchedHost || !watchedToken 
                    ? 'Enter HA host and token first'
                    : availableEntities.weather.length === 0
                    ? 'No weather entities found'
                    : 'Select weather entity'}
                </option>
                {availableEntities.weather.map((entity) => (
                  <option key={entity} value={entity}>
                    {entity}
                  </option>
                ))}
              </select>
            </div>

            <div className="mb-4">
              <label className="block text-sm font-medium mb-2 text-epd-black">
                Quote Sensor Entity
                {!watchedHost || !watchedToken ? (
                  <span className="text-xs text-gray-500 ml-2">(Enter HA host and token first)</span>
                ) : availableEntities.sensors.length === 0 ? (
                  <span className="text-xs text-orange-500 ml-2">(No sensor entities found)</span>
                ) : null}
              </label>
              <select
                {...register('quoteEntity', { required: true })}
                disabled={!watchedHost || !watchedToken || availableEntities.sensors.length === 0}
                className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black disabled:bg-gray-100 disabled:cursor-not-allowed"
              >
                <option value="">
                  {!watchedHost || !watchedToken 
                    ? 'Enter HA host and token first'
                    : availableEntities.sensors.length === 0
                    ? 'No sensor entities found'
                    : 'Select quote sensor'}
                </option>
                {availableEntities.sensors.map((entity) => (
                  <option key={entity} value={entity}>
                    {entity}
                  </option>
                ))}
              </select>
            </div>

            <div className="mb-4">
              <label className="block text-sm font-medium mb-2 text-epd-black">
                Todo Entities (select multiple)
              </label>
              <div className="max-h-40 overflow-y-auto border border-gray-300 rounded p-2">
                {availableEntities.todos.length === 0 ? (
                  <div className="text-sm text-gray-500">No todo entities found</div>
                ) : (
                  availableEntities.todos.map((entity) => {
                    const todoEntities = watch('todoEntities') || [];
                    const isChecked = todoEntities.includes(entity);
                    return (
                      <label key={entity} className="flex items-center gap-2 p-1 hover:bg-gray-100">
                        <input
                          type="checkbox"
                          checked={isChecked}
                          onChange={(e) => {
                            const current = watch('todoEntities') || [];
                            if (e.target.checked) {
                              setValue('todoEntities', [...current, entity]);
                            } else {
                              setValue('todoEntities', current.filter((e: string) => e !== entity));
                            }
                          }}
                          className="rounded"
                        />
                        <span className="text-sm">{entity}</span>
                      </label>
                    );
                  })
                )}
              </div>
            </div>

            <div className="mb-4">
              <label className="block text-sm font-medium mb-2 text-epd-black">
                Calendar Entities (select multiple)
              </label>
              <div className="max-h-40 overflow-y-auto border border-gray-300 rounded p-2">
                {availableEntities.calendars.length === 0 ? (
                  <div className="text-sm text-gray-500">No calendar entities found</div>
                ) : (
                  availableEntities.calendars.map((entity) => {
                    const calendarEntities = watch('calendarEntities') || [];
                    const isChecked = calendarEntities.includes(entity);
                    return (
                      <label key={entity} className="flex items-center gap-2 p-1 hover:bg-gray-100">
                        <input
                          type="checkbox"
                          checked={isChecked}
                          onChange={(e) => {
                            const current = watch('calendarEntities') || [];
                            if (e.target.checked) {
                              setValue('calendarEntities', [...current, entity]);
                            } else {
                              setValue('calendarEntities', current.filter((e: string) => e !== entity));
                            }
                          }}
                          className="rounded"
                        />
                        <span className="text-sm">{entity}</span>
                      </label>
                    );
                  })
                )}
              </div>
            </div>
          </div>

          {/* OTA Configuration */}
          <div className="pb-6">
            <h2 className="text-xl font-semibold mb-4 text-epd-black">OTA Update Configuration</h2>

            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="block text-sm font-medium mb-2 text-epd-black">
                  Device IP Address
                </label>
                <input
                  type="text"
                  {...register('ip', { required: true })}
                  className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black"
                  placeholder="192.168.1.101"
                />
                {errors.ip && (
                  <span className="text-red-500 text-sm">Device IP is required</span>
                )}
              </div>

              <div>
                <label className="block text-sm font-medium mb-2 text-epd-black">
                  OTA Password
                </label>
                <input
                  type="password"
                  {...register('otaPassword', { required: true })}
                  className="w-full px-3 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-epd-black"
                  placeholder="epd47ota"
                />
                {errors.otaPassword && (
                  <span className="text-red-500 text-sm">OTA password is required</span>
                )}
              </div>
            </div>
          </div>

          {/* Submit Button */}
          <div className="flex gap-4">
            <button
              type="submit"
              disabled={loading}
              className="px-6 py-2 bg-epd-black text-white rounded hover:bg-epd-gray disabled:opacity-50"
            >
              {loading ? 'Saving...' : 'Save Configuration'}
            </button>
            <button
              type="button"
              onClick={() => router.push('/')}
              className="px-6 py-2 bg-gray-300 text-epd-black rounded hover:bg-gray-400"
            >
              Cancel
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}
