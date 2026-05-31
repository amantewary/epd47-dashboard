'use client';

import { useState, useEffect } from 'react';
import { format } from 'date-fns';
import { DashboardData, AppConfig } from '@/lib/types';
import Weather from './Weather';
import TodoList from './TodoList';
import CalendarEvents from './CalendarEvents';
import Quote from './Quote';

interface DashboardProps {
  config: AppConfig;
}

export default function Dashboard({ config }: DashboardProps) {
  const [data, setData] = useState<DashboardData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [currentDate, setCurrentDate] = useState(new Date());

  const fetchData = async () => {
    try {
      setLoading(true);
      setError(null);

      // Validate config
      if (!config.host || !config.token) {
        throw new Error('Home Assistant configuration is missing. Please check your settings.');
      }

      if (!config.weatherEntity || !config.quoteEntity) {
        throw new Error('Required entities are not configured. Please select weather and quote entities in settings.');
      }

      const baseUrl = '/api/ha';
      const requestConfig = {
        host: config.host,
        port: config.port,
        token: config.token,
      };
      const jsonPost = (path: string, body: Record<string, unknown>) =>
        fetch(`${baseUrl}/${path}`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({ ...requestConfig, ...body }),
        });

      // Build fetch promises - handle empty arrays
      const fetchPromises: Promise<Response>[] = [
        jsonPost('weather', { entity: config.weatherEntity }),
      ];

      if (config.todoEntities && config.todoEntities.length > 0) {
        fetchPromises.push(
          jsonPost('todos', { entities: config.todoEntities })
        );
      } else {
        // Return empty array if no todos configured
        fetchPromises.push(Promise.resolve(new Response(JSON.stringify([]), { status: 200 })));
      }

      if (config.calendarEntities && config.calendarEntities.length > 0) {
        fetchPromises.push(
          jsonPost('calendars', { entities: config.calendarEntities })
        );
      } else {
        // Return empty array if no calendars configured
        fetchPromises.push(Promise.resolve(new Response(JSON.stringify([]), { status: 200 })));
      }

      fetchPromises.push(
        jsonPost('quotes', { entity: config.quoteEntity })
      );

      const [weatherRes, todosRes, calendarsRes, quotesRes] = await Promise.all(fetchPromises);

      // Check for errors
      if (!weatherRes.ok) {
        const errorData = await weatherRes.json().catch(() => ({}));
        throw new Error(errorData.error || `Weather API error: ${weatherRes.status}`);
      }
      if (!todosRes.ok) {
        const errorData = await todosRes.json().catch(() => ({}));
        throw new Error(errorData.error || `Todos API error: ${todosRes.status}`);
      }
      if (!calendarsRes.ok) {
        const errorData = await calendarsRes.json().catch(() => ({}));
        throw new Error(errorData.error || `Calendars API error: ${calendarsRes.status}`);
      }
      if (!quotesRes.ok) {
        const errorData = await quotesRes.json().catch(() => ({}));
        throw new Error(errorData.error || `Quotes API error: ${quotesRes.status}`);
      }

      const [weather, todos, calendarEvents, quotes] = await Promise.all([
        weatherRes.json(),
        todosRes.json(),
        calendarsRes.json(),
        quotesRes.json(),
      ]);

      setData({
        weather,
        todos: Array.isArray(todos) ? todos : [],
        calendarEvents: Array.isArray(calendarEvents) ? calendarEvents : [],
        quotes: Array.isArray(quotes) ? quotes : [],
        currentQuoteIndex: 0,
      });
    } catch (err: any) {
      console.error('Dashboard fetch error:', err);
      setError(err.message || 'Failed to fetch data. Check browser console for details.');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchData();

    // Refresh weather, todos, and calendars every hour.
    const hourlyInterval = setInterval(() => {
      fetchData();
    }, 60 * 60 * 1000);

    // The displayed date changes once per day, so refresh it at midnight.
    let dateTimeout: number | undefined;
    const scheduleDateRefresh = () => {
      const now = new Date();
      const nextMidnight = new Date(now);
      nextMidnight.setHours(24, 0, 0, 0);

      const delay = nextMidnight.getTime() - now.getTime();
      dateTimeout = window.setTimeout(() => {
        setCurrentDate(new Date());
        scheduleDateRefresh();
      }, delay);
    };
    scheduleDateRefresh();

    return () => {
      clearInterval(hourlyInterval);
      if (dateTimeout !== undefined) {
        clearTimeout(dateTimeout);
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [config]);

  // Firmware rotates quotes every 6 hours.
  useEffect(() => {
    if (!data || data.quotes.length === 0) return;

    const rotateInterval = setInterval(() => {
      setData(prev => {
        if (!prev) return prev;
        const nextIndex = (prev.currentQuoteIndex + 1) % prev.quotes.length;
        return { ...prev, currentQuoteIndex: nextIndex };
      });
    }, 6 * 60 * 60 * 1000);

    return () => clearInterval(rotateInterval);
  }, [data]);

  if (loading) {
    return (
      <div className="w-[960px] h-[540px] bg-epd-white flex items-center justify-center">
        <div className="text-epd-black">Loading...</div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="w-[960px] h-[540px] bg-epd-white flex flex-col items-center justify-center p-8">
        <div className="text-epd-black text-lg font-semibold mb-2">Error Loading Dashboard</div>
        <div className="text-epd-black text-sm mb-4 text-center">{error}</div>
        <button
          onClick={() => fetchData()}
          className="px-4 py-2 bg-epd-black text-epd-white rounded hover:bg-epd-gray text-sm"
        >
          Retry
        </button>
      </div>
    );
  }

  if (!data) {
    return null;
  }

  return (
    <div className="w-[960px] h-[540px] bg-epd-white relative overflow-hidden border border-epd-gray/20">
      <div className="absolute left-[20px] top-[20px] w-[220px] h-[35px] flex items-center">
        <div className="text-[22px] font-semibold leading-none text-epd-black">
          {format(currentDate, 'EEE MMM dd')}
        </div>
      </div>

      <div className="absolute left-[820px] top-[20px] w-[120px] h-[35px]">
        <Weather weather={data.weather} />
      </div>

      <div className="absolute left-[20px] top-[60px] w-[920px] h-[70px] overflow-hidden">
        <Quote quotes={data.quotes} currentIndex={data.currentQuoteIndex} />
      </div>
      <div className="absolute left-[20px] top-[125px] w-[920px] border-t border-dashed border-epd-black" />

      <div className="absolute left-[20px] top-[175px] w-[450px] h-[345px] overflow-hidden">
        <TodoList todos={data.todos} />
      </div>
      <div className="absolute left-[490px] top-[175px] w-[450px] h-[345px] overflow-hidden">
        <CalendarEvents events={data.calendarEvents} />
      </div>
    </div>
  );
}
