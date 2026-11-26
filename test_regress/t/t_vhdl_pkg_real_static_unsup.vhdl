-- DESCRIPTION: Verilator: VHDL real package constant with a non-static value is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package pk is
  function f (x : integer) return real;
  constant r : real := f(3);
end package pk;
package body pk is
  function f (x : integer) return real is variable acc : real := 0.0; begin for i in 1 to x loop acc := acc + 1.5; end loop; return acc; end function f;
end package body pk;
entity t is port (o : out bit); end entity t;
architecture rtl of t is begin o <= '1'; end architecture rtl;
