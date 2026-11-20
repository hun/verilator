-- DESCRIPTION: Verilator: a package specification constant initialized by a body function
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package pkg is
  function first_length(a, b : integer) return integer;
  -- The initializer calls a subprogram whose body the specification's own pass cannot read
  constant picked : integer := first_length(5, 2);
  -- A deferred constant is published before the next one is completed, so this one sees it
  constant twice : integer := first_length(picked, 3);
end package pkg;

package body pkg is
  function first_length(a, b : integer) return integer is
  begin
    return a + b;
  end function first_length;
end package body pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg.all;

entity t is
  port (
    o_picked : out integer;
    o_twice  : out integer
  );
end entity t;

architecture rtl of t is
begin
  o_picked <= picked;
  o_twice  <= twice;
end architecture rtl;