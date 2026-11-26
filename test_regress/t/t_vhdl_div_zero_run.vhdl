-- DESCRIPTION: Verilator: a division by zero that runs
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (a : in integer range 0 to 100;
        b : in integer range 0 to 100;
        guard : in std_logic;
        plain : out integer range 0 to 100;
        skipped : out integer range 0 to 100);
end entity t;

architecture rtl of t is
begin
  -- A zero divisor is an error where the division runs (IEEE 1076-2008 5.2.3.1)
  plain <= a / b when guard = '0' else 0;

  process (a, b, guard) is
  begin
    if guard = '1' and b /= 0 then
      skipped <= a / b;
    else
      skipped <= 0;
    end if;
  end process;
end architecture rtl;
