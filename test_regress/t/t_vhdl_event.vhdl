-- DESCRIPTION: Verilator: VHDL clk'event clock idiom
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    d   : in std_logic;
    q   : out std_logic
  );
end;

architecture rtl of t is
begin
  process (clk) is
  begin
    if clk'event and clk = '1' then
      q <= d;
    end if;
  end process;
end;
