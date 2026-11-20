-- DESCRIPTION: Verilator: VHDL exhaustive case over an integer subtype
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A selector of `integer range 0 to 3` is covered by its four values; no
-- `others` arm is required (IEEE 1076-2008 10.9). GHDL accepts this design.

entity t is
  port (
    s : in  integer range 0 to 3;
    y : out bit;
    z : out bit
  );
end entity;

architecture rtl of t is
begin
  process (s)
  begin
    case s is
      when 0 => y <= '0';
      when 1 => y <= '1';
      when 2 => y <= '0';
      when 3 => y <= '1';
    end case;
  end process;

  with s select
    z <= '0' when 0,
         '1' when 1,
         '0' when 2,
         '1' when 3;
end architecture;
