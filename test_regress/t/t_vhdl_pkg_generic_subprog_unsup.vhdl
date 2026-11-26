-- DESCRIPTION: Verilator: VHDL subprogram generic actual on a package instance is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package gp is
  generic (function f (x : integer) return integer);
end package gp;
package pk is
  function g (x : integer) return integer;
end package pk;
package body pk is
  function g (x : integer) return integer is begin return x; end function g;
end package body pk;
package inst is new work.gp generic map (f => work.pk.g);
entity t is port (o : out bit); end entity t;
architecture rtl of t is begin o <= '1'; end architecture rtl;
