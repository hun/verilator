-- DESCRIPTION: Verilator: VHDL metavalue choice diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A metavalue choice is legal VHDL (GHDL and nvc accept both statements), but
-- the two-state model has no value for 'X' to match. The frontend must report
-- it cleanly rather than assert.

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
