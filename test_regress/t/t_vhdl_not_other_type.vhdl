-- DESCRIPTION: Verilator: a unary operator declared for another type does not hide not
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package other is
  type other_t is (first, second);
  function "not"(a : other_t) return other_t;
  function same(a : string; b : string) return boolean;
end package other;

package body other is
  function "not"(a : other_t) return other_t is
  begin
    if a = first then
      return second;
    end if;
    return first;
  end function;
  function same(a : string; b : string) return boolean is
  begin
    return a = b;
  end function;
end package body other;

library ieee;
use ieee.std_logic_1164.all;
use work.other.all;

entity t is
  port (
    n : in  natural range 0 to 3;
    q : out std_logic
  );
end entity t;

architecture rtl of t is
begin
  -- The operand of not is a boolean, so the package's operator on other_t does not apply
  q <= '1' when (n /= 0) and not same("a", "b") else '0';
end architecture rtl;
