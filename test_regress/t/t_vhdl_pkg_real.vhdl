-- DESCRIPTION: Verilator: a package's real constant is visible outside the package
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package pkg is
  constant half : real := 0.5;
end package pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg.all;

entity t is
  port (
    o_is_half : out integer;
    o_int     : out integer
  );
end entity t;

architecture rtl of t is
  -- A real constant only survives as an elaboration-time value, so its uses are folded
  constant is_half : boolean := half = 0.5;
  constant doubled : real    := half * 2.0;
  constant as_int  : integer := integer(doubled);
begin
  o_is_half <= 1 when is_half else 0;
  o_int     <= as_int;
end architecture rtl;
