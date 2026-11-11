-- DESCRIPTION: Verilator: VHDL choice lists with alternatives and ranges
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    idx : in integer range 0 to 7;
    sel : in std_logic_vector(1 downto 0);
    y   : out std_logic_vector(1 downto 0);
    w   : out std_logic_vector(1 downto 0);
    m   : out std_logic_vector(5 downto 0);
    q   : out std_logic_vector(3 downto 0);
    p   : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  -- Alternatives may be followed by a discrete range in the same choice list
  constant AGG : std_logic_vector(5 downto 0) := (0 | 1 to 2 => '1', 3 | 4 to 5 => '0');
  -- A range association whose value spans the range fills it in order
  constant SL : std_logic_vector(3 downto 0) := (1 to 2 => "10", others => '0');
begin
  process (idx, sel)
    variable v : std_logic_vector(3 downto 0);
  begin
    case idx is
      when 0 | 1 to 2 => y <= "01";
      when 3 | 4 => y <= "10";
      when others => y <= "11";
    end case;
    v := (0 | 1 to 2 => '1', others => '0');
    p <= v;
  end process;

  with sel select
    w <= "01" when "00" | "01",
         "10" when "10",
         "11" when others;

  m <= AGG;
  q <= SL;
end;
