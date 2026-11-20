-- DESCRIPTION: Verilator: a conversion to a real has no synthesizable form
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
    o : out std_logic
  );
end entity t;

architecture rtl of t is
  function to_real(value : integer) return real is
  begin
    return real(value);
  end function to_real;
begin
  -- Both operands are calls, so the conversion inside each is lowered
  o <= '1' when to_real(3) > to_real(0) else '0';
end architecture rtl;