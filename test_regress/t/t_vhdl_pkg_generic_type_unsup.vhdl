-- DESCRIPTION: Verilator: VHDL type generic actual given as a selected name is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package gp is
  generic (type t);
end package gp;
package tp is type tt is range 0 to 3; end package tp;
package inst is new work.gp generic map (t => work.tp.tt);
entity t is port (o : out bit); end entity t;
architecture rtl of t is begin o <= '1'; end architecture rtl;
