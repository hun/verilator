-- DESCRIPTION: Verilator: VHDL process(all)
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a   : in std_logic;
    b   : in std_logic;
    c   : in std_logic;
    sel : in std_logic;
    y   : out std_logic;
    z   : out std_logic;
    v   : out std_logic
  );
end;

architecture rtl of t is
begin
  process (all) is
  begin
    y <= a and b;
  end process;

  -- A variable is not a sensitivity signal
  process (all) is
    variable tmp : std_logic;
  begin
    tmp := a xor b;
    z <= tmp and c;
  end process;

  process (all) is
  begin
    if sel = '1' then
      v <= a;
    else
      v <= b;
    end if;
  end process;
end;
