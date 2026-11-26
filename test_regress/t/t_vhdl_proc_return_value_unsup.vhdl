-- DESCRIPTION: Verilator: a return with a value in a procedure is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A procedure return must not give a value (IEEE 1076-2008 10.8).
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is
  procedure pr(x : in bit) is
  begin
    return 1;
  end procedure;
begin
  process (a)
  begin
    pr(a);
    y(0) <= a;
  end process;
end architecture rtl;
