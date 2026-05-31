'use client';

import { useState, useEffect } from 'react';
import { format } from 'date-fns';

export default function Clock() {
  const [time, setTime] = useState(new Date());

  useEffect(() => {
    let timer: number | undefined;

    const scheduleTick = () => {
      const now = new Date();
      setTime(now);

      const nextMinute = new Date(now);
      nextMinute.setSeconds(0, 0);
      nextMinute.setMinutes(nextMinute.getMinutes() + 1);

      timer = window.setTimeout(scheduleTick, nextMinute.getTime() - now.getTime());
    };

    scheduleTick();

    return () => {
      if (timer !== undefined) {
        clearTimeout(timer);
      }
    };
  }, []);

  return (
    <div className="text-2xl font-medium text-epd-black">
      {format(time, 'h:mm')}
    </div>
  );
}
