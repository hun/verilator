-- DESCRIPTION: Verilator: VHDL physical (time) literals and the time type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    q_ratio : out integer;  -- 200 ns / 8 ns
    q_half  : out integer;  -- (60 ns / 2) / 1 ns
    q_big   : out integer;  -- 1 sec / 1 ms, a time intermediate over int32
    q_frac  : out integer   -- 27.777 us / 83.333 ns
  );
end t;

architecture behav of t is
  constant a_ns : time := 200 ns;
  constant b_ns : time := 8 ns;
begin
  -- A physical literal is a count of the step unit; dividing two times gives an integer
  q_ratio <= a_ns / b_ns;
  -- time / integer is time, time / time is integer
  q_half <= (60 ns / 2) / 1 ns;
  -- 1 sec is 1e15 fs, far beyond int32, so this only comes out as 1000 when the physical
  -- values are held in 64 bits
  q_big <= 1 sec / 1 ms;
  -- A fractional numeric part is scaled to the unit and rounded to whole steps
  q_frac <= 27.777 us / 83.333 ns;
end behav;
