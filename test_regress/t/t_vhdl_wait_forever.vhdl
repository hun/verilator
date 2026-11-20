-- DESCRIPTION: Verilator: VHDL process that suspends forever with `wait;`
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic;
    q   : out integer;
    r   : out integer
  );
end entity t;

architecture rtl of t is
  signal one_shot : integer := 0;
  signal ticks    : integer := 0;
begin

  -- A bare wait suspends the process forever: the statements before it run once
  -- at time zero and are never reached again.
  p : process
  begin
    one_shot <= 7;
    report "one-shot process";
    wait;
  end process;

  -- Proves the one-shot process is not restarted by clock activity
  c : process (clk)
  begin
    if rising_edge(clk) then
      ticks <= ticks + 1;
    end if;
  end process;

  q <= one_shot;
  r <= ticks;

end architecture rtl;
