-- DESCRIPTION: Verilator: VHDL case, variables, indexing and slicing
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    sel : in std_logic_vector(1 downto 0);
    a   : in std_logic_vector(3 downto 0);
    y   : out std_logic;
    z   : out std_logic_vector(1 downto 0);
    w   : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
begin
  process (sel, a) is
    variable v  : std_logic;
    variable vv : std_logic_vector(1 downto 0);
  begin
    v := a(0);
    vv := a(1 downto 0);
    z <= vv;
    w(0) <= a(2);
    w(1) <= a(3);
    case sel is
      when "00" => y <= v;
      when "01" => y <= a(3);
      when "10" | "11" => y <= '1';
      when others => y <= '0';
    end case;
  end process;
end;
