-- DESCRIPTION: Verilator: a process loop range that reads a signal has to fold
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package pkg is
  function pick(v : integer) return integer;
end package pkg;

package body pkg is
  function pick(v : integer) return integer is
  begin
    return v + 100;
  end function pick;
end package body pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg.all;

entity t is
  port (
    n : in  integer;
    o : out integer
  );
end entity t;

architecture rtl of t is
begin
  proc : process (n)
    variable acc : integer := 0;
  begin
    -- A sequential loop range is evaluated when the loop is reached, so it may read n
    for i in 1 to pick(n) - 100 loop
      acc := acc + i;
    end loop;
    o <= acc;
  end process proc;
end architecture rtl;
