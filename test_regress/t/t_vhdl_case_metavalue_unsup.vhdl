-- DESCRIPTION: Verilator: VHDL metavalue choice diagnostic
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A metavalue choice is legal VHDL (GHDL and nvc accept both statements), but
-- the two-state model has no value for 'X' to match. The frontend must report
-- it cleanly; it used to assert in V3Number::toUInt.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    s : in  std_logic;
    v : in  std_logic_vector(1 downto 0);
    y : out std_logic
  );
end entity;

architecture rtl of t is
begin
  process (s)
  begin
    case s is
      when 'X'    => y <= '1';
      when others => y <= '0';
    end case;
  end process;

  process (v)
  begin
    case v is
      when "XX"   => y <= '1';
      when others => y <= '0';
    end case;
  end process;
end architecture;
