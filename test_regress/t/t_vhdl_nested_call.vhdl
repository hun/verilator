-- DESCRIPTION: Verilator: a call of the same function nested in another call's actuals
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
  -- neorv32's top selects its boot address with a call nested in another call of the
  -- same function
  function pick(sel : boolean; a, b : std_logic_vector(3 downto 0))
    return std_logic_vector is
  begin
    if sel then
      return a;
    end if;
    return b;
  end function;

  constant chosen : std_logic_vector(3 downto 0)
    := pick(true, "0001", pick(false, "0010", "0100"));
begin
  o <= chosen;
end architecture rtl;
